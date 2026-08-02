#pragma once

#include <QString>
#include <QStringList>

class ProjectScaffolder {
public:
    // The name reaches generated CMake and generated QML, so it has to be
    // checked before anything is written. An empty name used to scaffold into
    // the current directory, and "My App" produced CMake that cannot parse.
    static bool isValidProjectName(const QString &name, QString *error = nullptr);

    struct Options {
        QString organization = QStringLiteral("dev.example");
        // The platforms the generated application declares. Empty means all of
        // ProjectManifest::supportedPlatforms(): a project that has not said
        // otherwise is not committing to anything by listing them, and adding a
        // platform later is one line of loom.json.
        QStringList platforms;
        // Off by default. The generated workflow cannot install loom without a
        // published release, so scaffolding one by default gave every new
        // project a guaranteed red X on its first push.
        bool githubWorkflow = false;
    };

    static bool create(
        const QString &name, const QString &destination, const Options &options,
        QString *error = nullptr);
};
