// loomstyle -- offline access to the `Lo.style` vocabulary.
//
//   loomstyle --check PATH...      validate every Lo.style literal under PATH
//   loomstyle --dump-catalogue     print the vocabulary as JSON
//
// The checker exists because Loom's own diagnostics are necessarily runtime
// ones: an unknown class warns when the styled item is created, in a log
// nobody is watching, and only for code paths that actually run. Parsing the
// QML for literals catches the same typos at build time.

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

#include <loom/loom.h>
#include <loom/loomcatalogue.h>

namespace {

struct Finding {
    QString file;
    int line = 0;
    QString klass;
};

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

QList<Finding> checkFile(const QString &path, QString *error)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly | QIODevice::Text)) {
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

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("loomstyle"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(loom::version()));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Inspect and check the Lo.style utility vocabulary."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption dump(
        QStringLiteral("dump-catalogue"),
        QStringLiteral("Print the utility vocabulary as JSON."));
    const QCommandLineOption check(
        QStringLiteral("check"),
        QStringLiteral("Report unknown classes in Lo.style literals."));
    const QCommandLineOption config(
        QStringLiteral("config"),
        QStringLiteral("Load a Loom JSON config first, so project-defined tokens count."),
        QStringLiteral("file"));
    parser.addOption(dump);
    parser.addOption(check);
    parser.addOption(config);
    parser.addPositionalArgument(
        QStringLiteral("paths"), QStringLiteral("Files or directories to check."));
    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);

    if (parser.isSet(config) && !loom::loadConfig(parser.value(config))) {
        err << "loomstyle: could not load config " << parser.value(config) << '\n';
        return 2;
    }

    if (parser.isSet(dump)) {
        out << QString::fromUtf8(loom::styleCatalogueJson());
        return 0;
    }

    if (!parser.isSet(check)) {
        parser.showHelp(2);
        return 2;
    }

    const QStringList paths = parser.positionalArguments();
    if (paths.isEmpty()) {
        err << "loomstyle: --check needs at least one path\n";
        return 2;
    }

    QList<Finding> findings;
    int scanned = 0;
    for (const QString &path : paths) {
        if (!QFileInfo::exists(path)) {
            err << "loomstyle: no such path " << path << '\n';
            return 2;
        }
        for (const QString &file : qmlFilesUnder(path)) {
            QString error;
            findings.append(checkFile(file, &error));
            if (!error.isEmpty()) {
                err << "loomstyle: " << error << '\n';
                return 2;
            }
            ++scanned;
        }
    }

    for (const Finding &finding : findings) {
        err << finding.file << ':' << finding.line << ": unknown utility class '"
            << finding.klass << "'\n";
    }
    if (!findings.isEmpty()) {
        err << "loomstyle: " << findings.size() << " unknown class(es) in " << scanned
            << " file(s)\n";
        return 1;
    }
    out << "loomstyle: " << scanned << " file(s) checked, no unknown classes\n";
    return 0;
}
