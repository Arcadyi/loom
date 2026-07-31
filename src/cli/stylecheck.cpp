#include "stylecheck.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <loom/loomcatalogue.h>

namespace stylecheck {
namespace {

// Only literals can be checked. `Lo.style: someBinding` is skipped silently --
// reporting it would punish a legitimate pattern the checker simply cannot see
// into.
const QRegularExpression &literalPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"RX("((?:[^"\\]|\\.)*)"|'((?:[^'\\]|\\.)*)')RX"));
    return pattern;
}

// A binding may wrap across lines as a ternary or a `+` concatenation, with the
// operator trailing one line or leading the next -- qmlformat produces the
// latter. Accept both shapes; anything subtler needs a real QML parser, and
// missing a literal only costs coverage, never a false positive.
bool continuesInto(const QString &current, const QString &next)
{
    const QString from = current.trimmed();
    const QString to = next.trimmed();
    return from.endsWith(QLatin1Char('+')) || from.endsWith(QLatin1Char('?'))
        || from.endsWith(QLatin1Char(':')) || from.endsWith(QLatin1Char('('))
        || to.startsWith(QLatin1Char('+')) || to.startsWith(QLatin1Char('?'))
        || to.startsWith(QLatin1Char(':'));
}

// `"… rounded-" + model.key` is the idiomatic way to build a class from a
// binding, and its literal half ends on a dangling prefix that cannot parse on
// its own. Only the trailing class is exempt: a `bg-` in the middle of a string
// is still a typo worth reporting.
bool isConcatenationPrefix(const QString &literal, const QString &klass)
{
    if (!klass.endsWith(QLatin1Char('-')))
        return false;
    const QStringList classes = literal.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return !classes.isEmpty() && classes.constLast() == klass;
}

void collectFromLine(
    const QString &file, int lineNumber, const QString &text, QList<Finding> *findings)
{
    auto matches = literalPattern().globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const QString literal =
            match.captured(1).isNull() ? match.captured(2) : match.captured(1);
        for (const QString &klass : loom::unknownStyleClasses(literal)) {
            if (isConcatenationPrefix(literal, klass))
                continue;
            findings->append(Finding{file, lineNumber, klass});
        }
    }
}

} // namespace

QList<Finding> checkFile(const QString &path, QString *error)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("could not read %1").arg(path);
        return {};
    }
    const QStringList lines = QString::fromUtf8(input.readAll()).split(QLatin1Char('\n'));

    QList<Finding> findings;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const qsizetype marker = lines.at(i).indexOf(QLatin1String("Lo.style"));
        if (marker < 0)
            continue;
        collectFromLine(path, int(i + 1), lines.at(i).mid(marker), &findings);
        // Bounded, so a stray trailing colon cannot walk the rest of the file.
        constexpr qsizetype MaxContinuationLines = 8;
        for (qsizetype j = i; j < lines.size() - 1 && j - i < MaxContinuationLines
             && continuesInto(lines.at(j), lines.at(j + 1));
             ++j) {
            collectFromLine(path, int(j + 2), lines.at(j + 1), &findings);
        }
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
