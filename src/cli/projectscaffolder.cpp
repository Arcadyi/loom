#include "projectscaffolder.h"

#include "projectmanifest.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <utility>

namespace {

// Escapes a display name for a QML double-quoted string literal, which is where
// it ends up (qsTr("...") in Main.qml). A name containing a quote or a
// backslash otherwise generates QML that does not parse.
QString qmlStringLiteral(QString value)
{
    value.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    value.replace(QLatin1Char('"'), QLatin1String("\\\""));
    return value;
}

// A generated project may be opened directly in an IDE, without `loom build`
// adding the package prefix to CMake's command line. Record the package that
// belongs to the running CLI so that this first configure is deterministic.
// The template only uses the hint while it exists, which keeps a copied project
// portable to another developer or machine.
QString ownCmakePackageDirectory()
{
    const QDir binaryDirectory(QCoreApplication::applicationDirPath());
    const QStringList candidates{
        // Build-tree CLI: build/loom and build/loomConfig.cmake.
        binaryDirectory.absolutePath(),
        // Installed CLI: <prefix>/bin/loom and
        // <prefix>/<libdir>/cmake/loom/loomConfig.cmake.
        QDir::cleanPath(binaryDirectory.filePath(
            QStringLiteral("../") + QStringLiteral(LOOM_RELATIVE_CMAKE_DIR))),
    };
    for (const auto &candidate : candidates) {
        if (QFileInfo(QDir(candidate).filePath(QStringLiteral("loomConfig.cmake")))
                .isFile()) {
            return QDir::fromNativeSeparators(candidate);
        }
    }
    return {};
}

// The value is substituted into a CMake double-quoted argument. Paths normally
// contain none of these characters, but escaping them makes a custom install
// prefix data rather than generated CMake syntax.
QString cmakeStringLiteral(QString value)
{
    value = QDir::fromNativeSeparators(value);
    value.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    value.replace(QLatin1Char('"'), QLatin1String("\\\""));
    value.replace(QLatin1Char('$'), QLatin1String("\\$"));
    value.replace(QLatin1Char(';'), QLatin1String("\\;"));
    return value;
}

// Do not write a developer's home directory into a CMakeLists.txt that will
// normally be committed. CMake expands the environment reference when it
// configures the project, so the hint also has a chance to work for another
// developer using the same documented install layout.
QString cmakePathExpression(const QString &value)
{
    const auto path = QDir::fromNativeSeparators(value);
    const auto home = QDir::fromNativeSeparators(QDir::homePath());
    if (path == home || path.startsWith(home + QLatin1Char('/'))) {
#ifdef Q_OS_WIN
        const auto homeExpression = QStringLiteral("$ENV{USERPROFILE}");
#else
        const auto homeExpression = QStringLiteral("$ENV{HOME}");
#endif
        return homeExpression + cmakeStringLiteral(path.sliced(home.size()));
    }
    return cmakeStringLiteral(path);
}

// Ordered on purpose. A QHash iterates in an unspecified order, and these
// replacements are applied in sequence to the same buffer, so the order was
// silently part of the output.
using Replacement = std::pair<QString, QString>;

bool copyTemplate(
    const QString &sourceRoot, const QString &destinationRoot,
    const QList<Replacement> &replacements, const QStringList &skippedPrefixes,
    QString *error)
{
    QDirIterator iterator(
        sourceRoot, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const auto source = iterator.next();
        auto relative = QDir(sourceRoot).relativeFilePath(source);
        const bool skipped = std::any_of(
            skippedPrefixes.cbegin(), skippedPrefixes.cend(),
            [&relative](const QString &prefix) { return relative.startsWith(prefix); });
        if (skipped)
            continue;
        if (relative.endsWith(QStringLiteral(".in")))
            relative.chop(3);
        const auto destination = QDir(destinationRoot).filePath(relative);
        if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
            if (error)
                *error = QStringLiteral("Could not create %1")
                             .arg(QFileInfo(destination).absolutePath());
            return false;
        }

        QFile input(source);
        if (!input.open(QIODevice::ReadOnly)) {
            if (error)
                *error = input.errorString();
            return false;
        }
        QByteArray contents = input.readAll();
        for (const auto &[placeholder, value] : replacements)
            contents.replace(placeholder.toUtf8(), value.toUtf8());

        QSaveFile output(destination);
        if (!output.open(QIODevice::WriteOnly)
            || output.write(contents) != contents.size() || !output.commit()) {
            if (error)
                *error = QStringLiteral("Could not write %1").arg(destination);
            return false;
        }
    }
    return true;
}

} // namespace

bool ProjectScaffolder::isValidProjectName(const QString &name, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (name.isEmpty())
        return fail(QStringLiteral("project name must not be empty"));
    if (name.size() > 128)
        return fail(QStringLiteral("project name is too long (limit 128 characters)"));

    for (const auto character : name) {
        if (character.isSpace() || character.category() == QChar::Other_Control) {
            return fail(QStringLiteral(
                            "project name must not contain whitespace or control "
                            "characters: '%1'")
                            .arg(name));
        }
    }
    // Characters that would break out of the generated CMake or the generated
    // path, rather than merely look odd in a window title.
    static const QString forbidden = QStringLiteral("/\\:*?\"<>|$;()#");
    for (const auto character : forbidden) {
        if (name.contains(character)) {
            return fail(
                QStringLiteral("project name must not contain '%1'").arg(character));
        }
    }
    if (name == QStringLiteral(".") || name == QStringLiteral(".."))
        return fail(QStringLiteral("project name must not be '%1'").arg(name));
    // Everything is stripped to an identifier for the CMake target, so a name
    // made only of punctuation would silently become "App".
    if (identifierFromName(name) == QStringLiteral("App")
        && name.compare(QStringLiteral("App"), Qt::CaseInsensitive) != 0) {
        return fail(QStringLiteral(
                        "project name '%1' has no letters or digits to build a "
                        "CMake target name from")
                        .arg(name));
    }
    return true;
}

bool ProjectScaffolder::create(
    const QString &name, const QString &destination, const Options &options,
    QString *error)
{
    if (!isValidProjectName(name, error))
        return false;

    const QFileInfo destinationInfo(destination);
    // QDir::isEmpty() excludes hidden entries by default, so a directory holding
    // only .git or .gitignore looked empty and was scaffolded over.
    if (destinationInfo.exists()
        && !QDir(destination)
                .isEmpty(
                    QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden
                    | QDir::System)) {
        if (error)
            *error = QStringLiteral("Destination is not empty: %1").arg(destination);
        return false;
    }
    if (!QDir().mkpath(destination)) {
        if (error)
            *error = QStringLiteral("Could not create %1").arg(destination);
        return false;
    }

    const auto manifest = ProjectManifest::createDefault(name, options.organization);
    const auto application = manifest.primaryApplication();
    // The identifier goes into project() and the target name; the display name
    // survives only in QML and the README, and is escaped for its context.
    const QList<Replacement> replacements{
        Replacement{QStringLiteral("@LOOM_DISPLAY_NAME@"), qmlStringLiteral(name)},
        Replacement{QStringLiteral("@LOOM_PROJECT_NAME@"), name},
        Replacement{QStringLiteral("@LOOM_TARGET@"), identifierFromName(name)},
        Replacement{QStringLiteral("@LOOM_APP_ID@"), application.id},
        Replacement{QStringLiteral("@LOOM_URI@"), application.uri},
        Replacement{QStringLiteral("@LOOM_VERSION@"), QStringLiteral(LOOM_VERSION_STR)},
        Replacement{
            QStringLiteral("@LOOM_CMAKE_PACKAGE_DIR@"),
            cmakePathExpression(ownCmakePackageDirectory())},
    };

    QStringList skipped;
    if (!options.githubWorkflow)
        skipped.append(QStringLiteral(".github/"));
    if (!copyTemplate(
            QStringLiteral(LOOM_TEMPLATE_DIR), destination, replacements, skipped,
            error)) {
        return false;
    }

    // Created empty rather than seeded with a README: the asset root is globbed
    // wholesale into the binary, so any placeholder file ships inside every
    // application built from this template.
    if (!QDir(destination).mkpath(QStringLiteral("assets"))) {
        if (error)
            *error = QStringLiteral("Could not create the assets directory");
        return false;
    }
    return manifest.save(QDir(destination).filePath(QStringLiteral("loom.json")), error);
}
