#include "stylecheck.h"

#include "lspdocument.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <algorithm>
#include <tuple>

#include <loom/loomcatalogue.h>

#include "style/loomstylecompiler.h"
#include "tokens/loomtokenregistry.h"

namespace {

// Everything about a rule except what it writes: the conditions under which it
// applies. Two rules for the same utility under the same conditions are the
// same slot, and only one of them can win.
QString conditionKey(const LoomStyleRule &rule)
{
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|%13|%14")
        .arg(int(rule.utility))
        .arg(rule.stateMask)
        .arg(rule.stateNotMask)
        .arg(rule.minWidth)
        .arg(rule.maxWidth)
        .arg(rule.containerMinWidth)
        .arg(rule.containerMaxWidth)
        .arg(rule.groupStateMask)
        .arg(rule.groupStateNotMask)
        .arg(rule.customMask)
        .arg(rule.customNotMask)
        .arg(rule.groupCustomMask)
        .arg(rule.groupCustomNotMask)
        .arg(rule.containerName, rule.groupName, rule.themeName);
}

struct ClassOccurrence {
    QString name;
    qsizetype offset = 0;
};

// The classes of one literal, with where each starts, so a finding can point at
// the class rather than at the binding.
QList<ClassOccurrence> classesOf(const QString &literal, qsizetype base)
{
    QList<ClassOccurrence> classes;
    qsizetype i = 0;
    while (i < literal.size()) {
        while (i < literal.size() && literal.at(i).isSpace())
            ++i;
        const qsizetype start = i;
        while (i < literal.size() && !literal.at(i).isSpace())
            ++i;
        if (i > start)
            classes.append({literal.mid(start, i - start), base + start});
    }
    return classes;
}

// A line carrying `// loom-ignore <code>` suppresses that code on the next
// line. Without this any rule with a nonzero false-positive rate is
// un-adoptable in a real project, and the honest response to that would be to
// make the rule so conservative it finds nothing.
bool suppressed(const QStringList &lines, int line, const QString &code)
{
    if (line < 2 || line - 2 >= lines.size())
        return false;
    const QString previous = lines.at(line - 2);
    const qsizetype marker = previous.indexOf(QLatin1String("loom-ignore"));
    if (marker < 0)
        return false;
    static constexpr QLatin1StringView keyword("loom-ignore");
    const QStringView rest =
        QStringView(previous).sliced(marker + keyword.size()).trimmed();
    // Bare `loom-ignore` suppresses every code on the next line; naming one
    // suppresses only that.
    return rest.isEmpty() || rest.contains(code);
}

} // namespace

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

    // Second pass, per *literal* rather than per class: a class is only dead
    // relative to the others in the same string. The two branches of a ternary
    // are separate literals and legitimately write the same utility, which is
    // the entire point of writing one.
    const QStringList lines = source.split(QLatin1Char('\n'));
    for (const auto &literal : document.styleLiterals()) {
        const QString text = source.mid(
            literal.content.start, literal.content.end - literal.content.start);
        const auto classes = classesOf(text, literal.content.start);

        // Which condition slots each class fills. Not named `slots`: Qt
        // defines that as a macro expanding to nothing, so the declaration
        // silently becomes a statement that declares no variable.
        QList<QSet<QString>> filledSlots;
        QSet<QString> seenNames;
        filledSlots.reserve(classes.size());
        for (const auto &occurrence : classes) {
            QSet<QString> filled;
            const auto compiled = LoomStyleCompiler::compile(occurrence.name);
            for (const auto &rule : compiled->rules)
                filled.insert(conditionKey(rule));
            filledSlots.append(filled);
        }

        for (qsizetype index = 0; index < classes.size(); ++index) {
            const auto &occurrence = classes.at(index);
            const QJsonObject position =
                document.positionAt(occurrence.offset, lsp::PositionEncoding::Utf16);
            const int line = position.value(QStringLiteral("line")).toInt() + 1;
            const int column = position.value(QStringLiteral("character")).toInt() + 1;

            if (seenNames.contains(occurrence.name)) {
                if (!suppressed(lines, line, QStringLiteral("duplicateClass"))) {
                    findings.append(
                        Finding{
                            path,
                            line,
                            column,
                            occurrence.name,
                            QStringLiteral("duplicateClass"),
                            QStringLiteral("'%1' is already in this class string")
                                .arg(occurrence.name),
                            false,
                        });
                }
                continue;
            }
            seenNames.insert(occurrence.name);

            if (filledSlots.at(index).isEmpty())
                continue;
            // An exact repeat is reported once, as a duplicate, on the second
            // occurrence. It is also technically overridden, but saying so
            // twice about the same two words is noise.
            const bool repeatedVerbatim = std::any_of(
                classes.cbegin() + index + 1, classes.cend(),
                [&](const ClassOccurrence &later) {
                    return later.name == occurrence.name;
                });
            if (repeatedVerbatim)
                continue;
            // Dead only when *everything* it writes is written again later. A
            // partial overlap is the documented shorthand idiom -- `p-4 px-6`
            // sets four sides and then two of them -- and reporting that would
            // make the rule useless.
            bool entirelyShadowed = true;
            for (const QString &slot : filledSlots.at(index)) {
                bool covered = false;
                for (qsizetype later = index + 1; later < classes.size(); ++later) {
                    if (filledSlots.at(later).contains(slot)) {
                        covered = true;
                        break;
                    }
                }
                if (!covered) {
                    entirelyShadowed = false;
                    break;
                }
            }
            if (!entirelyShadowed)
                continue;
            if (suppressed(lines, line, QStringLiteral("conflictingClass")))
                continue;
            findings.append(
                Finding{
                    path,
                    line,
                    column,
                    occurrence.name,
                    QStringLiteral("conflictingClass"),
                    QStringLiteral(
                        "'%1' is overridden by a later class in the same string "
                        "and has no effect")
                        .arg(occurrence.name),
                    false,
                });
        }
    }

    std::sort(findings.begin(), findings.end(), [](const Finding &a, const Finding &b) {
        return std::tie(a.line, a.column) < std::tie(b.line, b.column);
    });
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
