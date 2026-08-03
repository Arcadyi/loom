#include "styleedit.h"

#include <QFile>
#include <QSaveFile>
#include <private/qqmljsast_p.h>
#include <private/qqmljsengine_p.h>
#include <private/qqmljslexer_p.h>
#include <private/qqmljsparser_p.h>

#include <loom/loomcatalogue.h>

namespace loom::styleedit {

namespace {

using namespace QQmlJS::AST;

struct Found {
    bool sawItem = false;    // something is declared at (line, column)
    bool sawBinding = false; // ... and it has an Lo.style binding
    bool literal = false;    // ... whose statement is a single string literal
    quint32 offset = 0;      // of the literal's *contents*, inside the quotes
    quint32 length = 0;
};

// Walks the document tracking the innermost enclosing object declaration, so an
// `Lo.style` binding can be attributed to the item that owns it. Matching on the
// declaration site rather than on an `id` is what makes this work for the items
// that have none -- most styled items -- and it means the several instances of
// one delegate all address the single place in the source that produced them.
class OwnerVisitor final : public Visitor {
public:
    OwnerVisitor(int line, int column, Found *found)
        : m_line(line)
        , m_column(column)
        , m_found(found)
    {
    }

    void throwRecursionDepthError() override
    {
    }

    bool visit(UiObjectDefinition *node) override
    {
        return enter(node->firstSourceLocation());
    }
    void endVisit(UiObjectDefinition *) override
    {
        leave();
    }
    bool visit(UiObjectBinding *node) override
    {
        return enter(node->firstSourceLocation());
    }
    void endVisit(UiObjectBinding *) override
    {
        leave();
    }

    bool visit(UiScriptBinding *binding) override
    {
        if (!m_matching || m_depthAtMatch != m_depth)
            return true;
        if (!binding->qualifiedId
            || binding->qualifiedId->toString() != QLatin1String("Lo.style"))
            return true;
        m_found->sawBinding = true;

        auto *statement = cast<ExpressionStatement *>(binding->statement);
        if (!statement)
            return false;
        // Deliberately only a bare string literal. A ternary or a concatenation
        // is refused rather than half-rewritten: the inspector reports the
        // evaluated result, which cannot tell us which branch produced it.
        auto *literal = cast<StringLiteral *>(statement->expression);
        if (!literal)
            return false;
        const auto token = literal->literalToken;
        if (token.length < 2)
            return false;
        m_found->literal = true;
        m_found->offset = token.offset + 1;
        m_found->length = token.length - 2;
        return false;
    }

private:
    bool enter(const QQmlJS::SourceLocation &location)
    {
        ++m_depth;
        if (!m_matching && int(location.startLine) == m_line
            && int(location.startColumn) == m_column) {
            m_matching = true;
            m_depthAtMatch = m_depth;
            m_found->sawItem = true;
        }
        return true;
    }

    void leave()
    {
        if (m_matching && m_depthAtMatch == m_depth)
            m_matching = false;
        --m_depth;
    }

    int m_line = 0;
    int m_column = 0;
    Found *m_found = nullptr;
    int m_depth = 0;
    int m_depthAtMatch = -1;
    bool m_matching = false;
};

Result refuse(const QString &reason)
{
    return Result{.ok = false, .error = reason, .updated = {}};
}

} // namespace

Result apply(
    const QString &text, const int line, const int column, const QString &oldStyle,
    const QString &newStyle)
{
    // Reuse the compiler's own view of the vocabulary, so the inspector can
    // never write a class string that `loom lint` would go on to reject.
    const QStringList unknown = loom::unknownStyleClasses(newStyle);
    if (!unknown.isEmpty()) {
        return refuse(
            QStringLiteral("unknown utility class '%1'").arg(unknown.constFirst()));
    }
    if (newStyle.contains(QLatin1Char('"')) || newStyle.contains(QLatin1Char('\\'))
        || newStyle.contains(QLatin1Char('\n'))) {
        // Nothing in the vocabulary needs them, and allowing them would mean
        // escaping correctly on the way into a source file.
        return refuse(QStringLiteral("style contains characters that cannot be written"));
    }

    QQmlJS::Engine engine;
    QQmlJS::Lexer lexer(&engine);
    engine.setLexer(&lexer);
    lexer.setCode(text, 1, true);
    QQmlJS::Parser parser(&engine);
    if (!parser.parse() || !parser.ast())
        return refuse(QStringLiteral("file does not parse as QML"));

    Found found;
    OwnerVisitor visitor(line, column, &found);
    parser.ast()->accept(&visitor);

    if (!found.sawItem)
        return refuse(
            QStringLiteral("no item is declared at %1:%2").arg(line).arg(column));
    if (!found.sawBinding) {
        return refuse(QStringLiteral("the item at %1:%2 has no Lo.style binding")
                          .arg(line)
                          .arg(column));
    }
    if (!found.literal) {
        return refuse(QStringLiteral("Lo.style at %1:%2 is computed, not a plain string")
                          .arg(line)
                          .arg(column));
    }

    const QString current = text.mid(found.offset, found.length);
    if (current != oldStyle) {
        // The file moved under the running scene. Refusing lets the reload
        // resynchronise instead of overwriting an edit made in an editor.
        return refuse(QStringLiteral("the file changed since the scene was built"));
    }
    if (current == newStyle)
        return Result{.ok = true, .error = {}, .updated = text};

    QString updated = text;
    updated.replace(found.offset, found.length, newStyle);
    return Result{.ok = true, .error = {}, .updated = updated};
}

Result applyToFile(
    const QString &path, const int line, const int column, const QString &oldStyle,
    const QString &newStyle)
{
    // Byte for byte, both ways. QIODevice::Text translates line endings on
    // Windows, so reading and writing it back through Text rewrote every line
    // of an LF file as CRLF -- one utility class edited from the inspector, and
    // the whole file shows as changed in the user's diff.
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly))
        return refuse(QStringLiteral("cannot read %1").arg(path));
    const QString text = QString::fromUtf8(source.readAll());
    source.close();

    Result result = apply(text, line, column, oldStyle, newStyle);
    if (!result.ok)
        return result;
    if (result.updated == text)
        return result;

    // Atomic: a crash between truncate and write would otherwise leave the user
    // with half a source file, which is a far worse outcome than a refused edit.
    QSaveFile destination(path);
    if (!destination.open(QIODevice::WriteOnly))
        return refuse(QStringLiteral("cannot write %1").arg(path));
    destination.write(result.updated.toUtf8());
    if (!destination.commit())
        return refuse(QStringLiteral("could not save %1").arg(path));
    return result;
}

} // namespace loom::styleedit
