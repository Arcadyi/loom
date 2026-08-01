#include "styleintelligence.h"

#include <QJsonDocument>
#include <QSet>
#include <algorithm>

#include "style/loomstylecompiler.h"

namespace lsp {

namespace {

QJsonObject textEdit(const QJsonObject &range, const QString &text)
{
    return {
        {QStringLiteral("range"), range},
        {QStringLiteral("newText"), text},
    };
}

QJsonObject markup(const QString &markdown)
{
    return {
        {QStringLiteral("kind"), QStringLiteral("markdown")},
        {QStringLiteral("value"), markdown},
    };
}

} // namespace

StyleIntelligence::StyleIntelligence(StyleWorkspace *workspace)
    : m_workspace(workspace)
{
}

QJsonObject StyleIntelligence::completion(
    const QString &filePath, const Document &document, qsizetype offset,
    PositionEncoding encoding, bool *inStyle)
{
    StyleToken token;
    const bool found = document.styleTokenAt(offset, &token);
    if (inStyle)
        *inStyle = found;
    if (!found)
        return {};

    m_workspace->activateForFile(filePath);
    const auto catalogue = m_workspace->catalogue();
    const qsizetype fragmentEnd =
        std::clamp<qsizetype>(offset, token.range.start, token.range.end);
    const QString typed =
        document.text().sliced(token.range.start, fragmentEnd - token.range.start);
    const QStringList segments = typed.split(QLatin1Char(':'));
    QStringList acceptedVariants;
    bool prefixValid = true;
    for (qsizetype index = 0; index + 1 < segments.size(); ++index) {
        if (!catalogue.variants.contains(segments.at(index))) {
            prefixValid = false;
            break;
        }
        acceptedVariants.append(segments.at(index));
    }
    const QString fragment = segments.isEmpty() ? QString() : segments.constLast();
    const QString acceptedPrefix = acceptedVariants.isEmpty()
        ? QString()
        : acceptedVariants.join(QLatin1Char(':')) + QLatin1Char(':');
    const QJsonObject replaceRange = document.rangeObject(token.range, encoding);

    QJsonArray items;
    for (const auto &variant : catalogue.variants) {
        if (acceptedVariants.contains(variant) || !variant.startsWith(fragment))
            continue;
        const QString value = acceptedPrefix + variant + QLatin1Char(':');
        items.append(
            QJsonObject{
                {QStringLiteral("label"), value},
                {QStringLiteral("kind"), 14}, // CompletionItemKind.Keyword
                {QStringLiteral("detail"), QStringLiteral("Loom variant")},
                {QStringLiteral("sortText"), QStringLiteral("0-") + value},
                {QStringLiteral("textEdit"), textEdit(replaceRange, value)},
            });
    }

    if (prefixValid) {
        for (const auto &klass : catalogue.classes) {
            if (!klass.startsWith(fragment))
                continue;
            const QString value = acceptedPrefix + klass;
            const ClassMetadata metadata = m_workspace->metadata(value);
            QJsonObject item{
                {QStringLiteral("label"), value},
                {QStringLiteral("kind"), 12}, // CompletionItemKind.Value
                {QStringLiteral("detail"), metadata.detail},
                {QStringLiteral("sortText"), QStringLiteral("1-") + value},
                {QStringLiteral("textEdit"), textEdit(replaceRange, value)},
            };
            if (!metadata.markdown.isEmpty())
                item.insert(QStringLiteral("documentation"), markup(metadata.markdown));
            items.append(item);
        }
    }
    return {
        {QStringLiteral("isIncomplete"), false},
        {QStringLiteral("items"), items},
    };
}

QJsonArray StyleIntelligence::diagnostics(
    const QString &filePath, const Document &document, PositionEncoding encoding)
{
    m_workspace->activateForFile(filePath);
    QJsonArray diagnostics;
    for (const auto &token : document.styleTokens()) {
        if (m_workspace->metadata(token.text).valid) {
            const QString policy = m_workspace->arbitraryValuePolicy();
            if (policy == QLatin1String("allow"))
                continue;
            const auto compiled = LoomStyleCompiler::compile(token.text);
            const bool arbitrary = std::any_of(
                compiled->rules.cbegin(), compiled->rules.cend(),
                [](const LoomStyleRule &rule) { return rule.arbitrary; });
            if (!arbitrary)
                continue;
            diagnostics.append(
                QJsonObject{
                    {QStringLiteral("range"),
                     document.rangeObject(token.range, encoding)},
                    {QStringLiteral("severity"), policy == QLatin1String("deny") ? 1 : 2},
                    {QStringLiteral("source"), QStringLiteral("loom")},
                    {QStringLiteral("code"), QStringLiteral("arbitraryValue")},
                    {QStringLiteral("message"),
                     QStringLiteral("Arbitrary value '%1' is %2 by design policy")
                         .arg(
                             token.text,
                             policy == QLatin1String("deny")
                                 ? QStringLiteral("denied")
                                 : QStringLiteral("discouraged"))},
                });
            continue;
        }
        const QStringList replacements = m_workspace->replacements(token.text);
        QJsonArray replacementJson;
        for (const auto &replacement : replacements)
            replacementJson.append(replacement);
        diagnostics.append(
            QJsonObject{
                {QStringLiteral("range"), document.rangeObject(token.range, encoding)},
                {QStringLiteral("severity"), 2},
                {QStringLiteral("source"), QStringLiteral("loom")},
                {QStringLiteral("code"), QStringLiteral("unknownUtility")},
                {QStringLiteral("message"),
                 QStringLiteral("Unknown Loom utility class '%1'").arg(token.text)},
                {QStringLiteral("data"),
                 QJsonObject{
                     {QStringLiteral("unknown"), token.text},
                     {QStringLiteral("replacements"), replacementJson},
                 }},
            });
    }
    return diagnostics;
}

QJsonValue StyleIntelligence::hover(
    const QString &filePath, const Document &document, qsizetype offset,
    PositionEncoding encoding)
{
    StyleToken token;
    if (!document.styleTokenAt(offset, &token) || token.text.isEmpty())
        return QJsonValue::Null;
    m_workspace->activateForFile(filePath);
    const ClassMetadata metadata = m_workspace->metadata(token.text);
    if (!metadata.valid)
        return QJsonValue::Null;
    return QJsonObject{
        {QStringLiteral("contents"), markup(metadata.markdown)},
        {QStringLiteral("range"), document.rangeObject(token.range, encoding)},
    };
}

QJsonArray StyleIntelligence::colors(
    const QString &filePath, const Document &document, PositionEncoding encoding)
{
    m_workspace->activateForFile(filePath);
    QJsonArray colors;
    for (const auto &token : document.styleTokens()) {
        const ClassMetadata metadata = m_workspace->metadata(token.text);
        if (!metadata.valid || !metadata.color.isValid())
            continue;
        const QColor color = metadata.color;
        colors.append(
            QJsonObject{
                {QStringLiteral("range"), document.rangeObject(token.range, encoding)},
                {QStringLiteral("color"),
                 QJsonObject{
                     {QStringLiteral("red"), color.redF()},
                     {QStringLiteral("green"), color.greenF()},
                     {QStringLiteral("blue"), color.blueF()},
                     {QStringLiteral("alpha"), color.alphaF()},
                 }},
            });
    }
    return colors;
}

QJsonArray
StyleIntelligence::codeActions(const QString &uri, const QJsonArray &diagnostics) const
{
    QJsonArray actions;
    for (const auto &entry : diagnostics) {
        const QJsonObject diagnostic = entry.toObject();
        if (diagnostic.value(QStringLiteral("source")).toString()
                != QStringLiteral("loom")
            || diagnostic.value(QStringLiteral("code")).toString()
                != QStringLiteral("unknownUtility")) {
            continue;
        }
        const QJsonObject data = diagnostic.value(QStringLiteral("data")).toObject();
        const QString unknown = data.value(QStringLiteral("unknown")).toString();
        for (const auto &replacementValue :
             data.value(QStringLiteral("replacements")).toArray()) {
            const QString replacement = replacementValue.toString();
            const QJsonObject edit = textEdit(
                diagnostic.value(QStringLiteral("range")).toObject(), replacement);
            actions.append(
                QJsonObject{
                    {QStringLiteral("title"),
                     QStringLiteral("Replace '%1' with '%2'").arg(unknown, replacement)},
                    {QStringLiteral("kind"), QStringLiteral("quickfix")},
                    {QStringLiteral("diagnostics"), QJsonArray{diagnostic}},
                    {QStringLiteral("edit"),
                     QJsonObject{
                         {QStringLiteral("changes"),
                          QJsonObject{{uri, QJsonArray{edit}}}},
                     }},
                });
        }
    }
    return actions;
}

} // namespace lsp
