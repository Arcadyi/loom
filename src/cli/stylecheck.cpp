#include "stylecheck.h"

#include "lspdocument.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <algorithm>

#include <loom/loomcatalogue.h>

#include "style/loomstylecompiler.h"
#include "tokens/loomtokenregistry.h"

namespace stylecheck {
QList<Finding> checkFile(const QString &path, QString *error)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("could not read %1").arg(path);
        return {};
    }
    const QString source = QString::fromUtf8(input.readAll());
    lsp::Document document;
    document.open(source, 1);

    QList<Finding> findings;
    const QString arbitraryPolicy = LoomTokenRegistry::instance()->arbitraryValuePolicy();
    for (const auto &token : document.styleTokens()) {
        const QJsonObject position =
            document.positionAt(token.range.start, lsp::PositionEncoding::Utf16);
        const int line = position.value(QStringLiteral("line")).toInt() + 1;
        const int column = position.value(QStringLiteral("character")).toInt() + 1;
        if (!loom::unknownStyleClasses(token.text).isEmpty()) {
            // A literal ending in a family prefix can be the static half of a
            // concatenation (`"bg-" + model.color`). The AST scanner has
            // already proven this string belongs to Lo.style; the dynamic half
            // cannot be checked offline.
            if (token.text.endsWith(QLatin1Char('-')))
                continue;
            findings.append(
                Finding{
                    path,
                    line,
                    column,
                    token.text,
                    QStringLiteral("unknownUtility"),
                    QStringLiteral("unknown utility class '%1'").arg(token.text),
                    true,
                });
            continue;
        }
        if (arbitraryPolicy == QLatin1String("allow"))
            continue;
        const auto compiled = LoomStyleCompiler::compile(token.text);
        const bool arbitrary = std::any_of(
            compiled->rules.cbegin(), compiled->rules.cend(),
            [](const LoomStyleRule &rule) { return rule.arbitrary; });
        if (!arbitrary)
            continue;
        findings.append(
            Finding{
                path,
                line,
                column,
                token.text,
                QStringLiteral("arbitraryValue"),
                QStringLiteral("arbitrary value '%1' is %2 by design policy")
                    .arg(
                        token.text,
                        arbitraryPolicy == QLatin1String("deny")
                            ? QStringLiteral("denied")
                            : QStringLiteral("discouraged")),
                arbitraryPolicy == QLatin1String("deny"),
            });
    }
    return findings;
}

QStringList qmlFilesUnder(const QString &path)
{
    if (QFileInfo(path).isFile())
        return {path};
    QStringList files;
    QDirIterator iterator(
        path, {QStringLiteral("*.qml")}, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext())
        files.append(iterator.next());
    files.sort();
    return files;
}

} // namespace stylecheck
