#include "lspdocument.h"

#include "stylebindings.h"

#include <algorithm>
#include <private/qqmljsast_p.h>
#include <private/qqmljsengine_p.h>
#include <private/qqmljslexer_p.h>
#include <private/qqmljsparser_p.h>

namespace lsp {

namespace {

bool identifierChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('$');
}

qsizetype skipSpace(const QString &text, qsizetype offset)
{
    while (offset < text.size() && text.at(offset).isSpace())
        ++offset;
    return offset;
}

qsizetype lineEnd(const QString &text, qsizetype offset)
{
    const qsizetype newline = text.indexOf(QLatin1Char('\n'), offset);
    return newline < 0 ? text.size() : newline;
}

bool bindingContinues(const QString &text, qsizetype currentEnd, qsizetype nextStart)
{
    qsizetype left = currentEnd;
    while (left > 0 && text.at(left - 1).isSpace()
           && text.at(left - 1) != QLatin1Char('\n'))
        --left;
    const QChar trailing = left > 0 ? text.at(left - 1) : QChar();
    const qsizetype right = skipSpace(text, nextStart);
    const QChar leading = right < text.size() ? text.at(right) : QChar();
    return trailing == QLatin1Char('+') || trailing == QLatin1Char('?')
        || trailing == QLatin1Char(':') || trailing == QLatin1Char('(')
        || trailing == QLatin1Char(',') || leading == QLatin1Char('+')
        || leading == QLatin1Char('?') || leading == QLatin1Char(':');
}

QVector<StyleLiteral> scanStyleLiteralsHeuristic(const QString &text)
{
    QVector<StyleLiteral> literals;
    enum class State { Code, LineComment, BlockComment, String };
    State state = State::Code;
    QChar quote;

    for (qsizetype i = 0; i < text.size();) {
        if (state == State::LineComment) {
            if (text.at(i++) == QLatin1Char('\n'))
                state = State::Code;
            continue;
        }
        if (state == State::BlockComment) {
            if (i + 1 < text.size() && text.at(i) == QLatin1Char('*')
                && text.at(i + 1) == QLatin1Char('/')) {
                i += 2;
                state = State::Code;
            } else {
                ++i;
            }
            continue;
        }
        if (state == State::String) {
            if (text.at(i) == QLatin1Char('\\')) {
                i += std::min<qsizetype>(2, text.size() - i);
            } else if (text.at(i++) == quote) {
                state = State::Code;
            }
            continue;
        }

        if (i + 1 < text.size() && text.at(i) == QLatin1Char('/')
            && text.at(i + 1) == QLatin1Char('/')) {
            i += 2;
            state = State::LineComment;
            continue;
        }
        if (i + 1 < text.size() && text.at(i) == QLatin1Char('/')
            && text.at(i + 1) == QLatin1Char('*')) {
            i += 2;
            state = State::BlockComment;
            continue;
        }
        if (text.at(i) == QLatin1Char('"') || text.at(i) == QLatin1Char('\'')) {
            quote = text.at(i++);
            state = State::String;
            continue;
        }

        // Every name that can carry a class string, not just `Lo.style`: the
        // part-style properties Loom.Controls forwards onto a sub-delegate are
        // ordinary QML properties, so nothing but the name identifies them.
        // QStringView rather than QString::mid, because this runs once per
        // character of the document and now against a list.
        QLatin1StringView marker;
        for (const char *candidate : loom::stylebindings::kNames) {
            const QLatin1StringView name(candidate);
            if (QStringView(text).mid(i, name.size()) != name)
                continue;
            if (i > 0 && identifierChar(text.at(i - 1)))
                continue;
            if (i + name.size() < text.size()
                && identifierChar(text.at(i + name.size())))
                continue;
            marker = name;
            break;
        }
        if (marker.isEmpty()) {
            ++i;
            continue;
        }

        qsizetype cursor = skipSpace(text, i + marker.size());
        if (cursor >= text.size() || text.at(cursor) != QLatin1Char(':')) {
            i += marker.size();
            continue;
        }
        ++cursor;

        qsizetype expressionEnd = lineEnd(text, cursor);
        for (int continuation = 0; continuation < 8 && expressionEnd < text.size();
             ++continuation) {
            if (!bindingContinues(text, expressionEnd, expressionEnd + 1))
                break;
            expressionEnd = lineEnd(text, expressionEnd + 1);
        }

        for (qsizetype j = cursor; j < expressionEnd;) {
            if (j + 1 < expressionEnd && text.at(j) == QLatin1Char('/')
                && text.at(j + 1) == QLatin1Char('/')) {
                j = lineEnd(text, j);
                continue;
            }
            if (j + 1 < expressionEnd && text.at(j) == QLatin1Char('/')
                && text.at(j + 1) == QLatin1Char('*')) {
                const qsizetype close = text.indexOf(QLatin1String("*/"), j + 2);
                j = close < 0 || close >= expressionEnd ? expressionEnd : close + 2;
                continue;
            }
            if (text.at(j) != QLatin1Char('"') && text.at(j) != QLatin1Char('\'')) {
                ++j;
                continue;
            }

            // A string immediately compared with ===/!== is a condition value,
            // not a fragment contributing classes to the binding result.
            qsizetype previous = j;
            while (previous > cursor && text.at(previous - 1).isSpace())
                --previous;
            const bool comparisonValue = previous > cursor
                && (text.at(previous - 1) == QLatin1Char('=')
                    || text.at(previous - 1) == QLatin1Char('!')
                    || text.at(previous - 1) == QLatin1Char('<')
                    || text.at(previous - 1) == QLatin1Char('>'));

            const QChar literalQuote = text.at(j++);
            const qsizetype start = j;
            while (j < expressionEnd && text.at(j) != literalQuote) {
                if (text.at(j) == QLatin1Char('\\') && j + 1 < expressionEnd)
                    j += 2;
                else
                    ++j;
            }
            qsizetype following = j < expressionEnd ? j + 1 : j;
            while (following < expressionEnd && text.at(following).isSpace())
                ++following;
            const bool comparedAfter = following < expressionEnd
                && (text.at(following) == QLatin1Char('=')
                    || text.at(following) == QLatin1Char('!')
                    || text.at(following) == QLatin1Char('<')
                    || text.at(following) == QLatin1Char('>'));
            if (!comparisonValue && !comparedAfter)
                literals.append(StyleLiteral{{start, j}});
            if (j < expressionEnd)
                ++j;
        }
        i = expressionEnd;
    }
    return literals;
}

void collectExpressionLiterals(
    QQmlJS::AST::ExpressionNode *expression, QVector<StyleLiteral> *literals)
{
    using namespace QQmlJS::AST;
    if (!expression)
        return;
    if (auto *literal = cast<StringLiteral *>(expression)) {
        const auto location = literal->literalToken;
        // The lexer location includes the quote characters. Keep source ranges
        // rather than decoded string values so LSP edits remain exact even for
        // escaped content.
        if (location.length >= 2)
            literals->append(
                StyleLiteral{
                    {qsizetype(location.offset + 1),
                     qsizetype(location.offset + location.length - 1)}});
        return;
    }
    if (auto *nested = cast<NestedExpression *>(expression)) {
        collectExpressionLiterals(nested->expression, literals);
        return;
    }
    if (auto *conditional = cast<ConditionalExpression *>(expression)) {
        // The condition is deliberately excluded. A comparison such as
        // `state === "active" ? "bg-accent" : "bg-surface"` contains three
        // strings but only the latter two contribute style classes.
        collectExpressionLiterals(conditional->ok, literals);
        collectExpressionLiterals(conditional->ko, literals);
        return;
    }
    if (auto *binary = cast<BinaryExpression *>(expression)) {
        switch (QSOperator::Op(binary->op)) {
        case QSOperator::Add:
        case QSOperator::And:
        case QSOperator::Or:
        case QSOperator::Coalesce:
            collectExpressionLiterals(binary->left, literals);
            collectExpressionLiterals(binary->right, literals);
            break;
        default:
            break;
        }
    }
}

class StyleBindingVisitor final : public QQmlJS::AST::Visitor {
public:
    explicit StyleBindingVisitor(QVector<StyleLiteral> *literals)
        : m_literals(literals)
    {
    }

    void throwRecursionDepthError() override
    {
    }

    bool visit(QQmlJS::AST::UiScriptBinding *binding) override
    {
        if (!binding->qualifiedId
            || !loom::stylebindings::isStyleBinding(binding->qualifiedId->toString()))
            return true;
        if (auto *statement = QQmlJS::AST::cast<QQmlJS::AST::ExpressionStatement *>(
                binding->statement)) {
            collectExpressionLiterals(statement->expression, m_literals);
        }
        // We already walked the only expression shapes whose string values can
        // flow into the binding result. A generic AST walk would reintroduce
        // comparison strings and function arguments as false positives.
        return false;
    }

private:
    QVector<StyleLiteral> *m_literals = nullptr;
};

QVector<StyleLiteral> scanStyleLiterals(const QString &text)
{
    QQmlJS::Engine engine;
    QQmlJS::Lexer lexer(&engine);
    engine.setLexer(&lexer);
    lexer.setCode(text, 1, true);
    QQmlJS::Parser parser(&engine);
    if (!parser.parse() || !parser.ast())
        return scanStyleLiteralsHeuristic(text);

    QVector<StyleLiteral> literals;
    StyleBindingVisitor visitor(&literals);
    parser.ast()->accept(&visitor);
    std::sort(literals.begin(), literals.end(), [](const auto &left, const auto &right) {
        return left.content.start < right.content.start;
    });
    return literals;
}

int encodedLength(QStringView text, PositionEncoding encoding)
{
    if (encoding == PositionEncoding::Utf16)
        return int(text.size());
    if (encoding == PositionEncoding::Utf8)
        return text.toUtf8().size();
    int count = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i).isHighSurrogate() && i + 1 < text.size()
            && text.at(i + 1).isLowSurrogate())
            ++i;
        ++count;
    }
    return count;
}

qsizetype utf16Offset(QStringView line, int character, PositionEncoding encoding)
{
    if (character < 0)
        return -1;
    if (encoding == PositionEncoding::Utf16)
        return std::min<qsizetype>(character, line.size());

    int consumed = 0;
    for (qsizetype i = 0; i < line.size();) {
        if (consumed >= character)
            return i;
        qsizetype units = 1;
        if (line.at(i).isHighSurrogate() && i + 1 < line.size()
            && line.at(i + 1).isLowSurrogate())
            units = 2;
        const QStringView scalar = line.mid(i, units);
        consumed += encoding == PositionEncoding::Utf8 ? scalar.toUtf8().size() : 1;
        i += units;
    }
    return line.size();
}

} // namespace

void Document::open(QString text, int version)
{
    m_text = std::move(text);
    m_version = version;
}

bool Document::applyChanges(
    const QJsonArray &changes, int version, PositionEncoding encoding, QString *error)
{
    for (const auto &entry : changes) {
        const QJsonObject change = entry.toObject();
        if (!change.contains(QStringLiteral("range"))) {
            m_text = change.value(QStringLiteral("text")).toString();
            continue;
        }
        const auto range = change.value(QStringLiteral("range")).toObject();
        const auto start = range.value(QStringLiteral("start")).toObject();
        const auto end = range.value(QStringLiteral("end")).toObject();
        bool startOk = false;
        bool endOk = false;
        const qsizetype startOffset = offsetAt(
            start.value(QStringLiteral("line")).toInt(-1),
            start.value(QStringLiteral("character")).toInt(-1), encoding, &startOk);
        const qsizetype endOffset = offsetAt(
            end.value(QStringLiteral("line")).toInt(-1),
            end.value(QStringLiteral("character")).toInt(-1), encoding, &endOk);
        if (!startOk || !endOk || startOffset > endOffset) {
            if (error)
                *error =
                    QStringLiteral("textDocument/didChange contains an invalid range");
            return false;
        }
        m_text.replace(
            startOffset, endOffset - startOffset,
            change.value(QStringLiteral("text")).toString());
    }
    m_version = version;
    return true;
}

QString Document::text() const
{
    return m_text;
}

int Document::version() const
{
    return m_version;
}

QVector<StyleLiteral> Document::styleLiterals() const
{
    return scanStyleLiterals(m_text);
}

QVector<StyleToken> Document::styleTokens() const
{
    QVector<StyleToken> tokens;
    for (const auto &literal : styleLiterals()) {
        qsizetype cursor = literal.content.start;
        while (cursor < literal.content.end) {
            while (cursor < literal.content.end && m_text.at(cursor).isSpace())
                ++cursor;
            const qsizetype start = cursor;
            while (cursor < literal.content.end && !m_text.at(cursor).isSpace())
                ++cursor;
            if (cursor > start)
                tokens.append({{start, cursor}, m_text.sliced(start, cursor - start)});
        }
    }
    return tokens;
}

bool Document::styleTokenAt(qsizetype offset, StyleToken *token) const
{
    for (const auto &literal : styleLiterals()) {
        if (offset < literal.content.start || offset > literal.content.end)
            continue;
        qsizetype start = offset;
        while (start > literal.content.start && !m_text.at(start - 1).isSpace())
            --start;
        qsizetype end = offset;
        while (end < literal.content.end && !m_text.at(end).isSpace())
            ++end;
        if (token)
            *token = {{start, end}, m_text.sliced(start, end - start)};
        return true;
    }
    return false;
}

qsizetype
Document::offsetAt(int line, int character, PositionEncoding encoding, bool *ok) const
{
    if (ok)
        *ok = false;
    if (line < 0 || character < 0)
        return 0;
    qsizetype start = 0;
    for (int currentLine = 0; currentLine < line; ++currentLine) {
        const qsizetype newline = m_text.indexOf(QLatin1Char('\n'), start);
        if (newline < 0)
            return m_text.size();
        start = newline + 1;
    }
    const qsizetype newline = m_text.indexOf(QLatin1Char('\n'), start);
    const qsizetype end = newline < 0 ? m_text.size() : newline;
    const qsizetype within =
        utf16Offset(QStringView(m_text).mid(start, end - start), character, encoding);
    if (within < 0)
        return start;
    if (ok)
        *ok = true;
    return start + within;
}

QJsonObject Document::positionAt(qsizetype offset, PositionEncoding encoding) const
{
    offset = std::clamp<qsizetype>(offset, 0, m_text.size());
    int line = 0;
    qsizetype lineStart = 0;
    for (qsizetype i = 0; i < offset; ++i) {
        if (m_text.at(i) == QLatin1Char('\n')) {
            ++line;
            lineStart = i + 1;
        }
    }
    return {
        {QStringLiteral("line"), line},
        {QStringLiteral("character"),
         encodedLength(QStringView(m_text).mid(lineStart, offset - lineStart), encoding)},
    };
}

QJsonObject
Document::rangeObject(const SourceRange &range, PositionEncoding encoding) const
{
    return {
        {QStringLiteral("start"), positionAt(range.start, encoding)},
        {QStringLiteral("end"), positionAt(range.end, encoding)},
    };
}

} // namespace lsp
