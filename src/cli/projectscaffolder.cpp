#include "projectscaffolder.h"

#include "projectmanifest.h"

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
    };

    QStringList skipped;
    if (!options.githubWorkflow)
        skipped.append(QStringLiteral(".github/"));
    if (!copyTemplate(
            QStringLiteral(LOOM_TEMPLATE_DIR), destination, replacements, skipped,
            error)) {
        return false;
    }
    // The loom overlay rewrites whole files over the base output rather than
    // splicing placeholders into it: the two variants stay readable, at the
    // cost of keeping the overlay in sync with the base template by hand.
    if (options.loom
        && !copyTemplate(
            QStringLiteral(LOOM_TEMPLATE_LOOM_DIR), destination, replacements, {},
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
    return manifest.save(
        QDir(destination).filePath(QStringLiteral("loom.json")), error);
}
