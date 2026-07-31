#pragma once

#include <QString>

class ProjectScaffolder {
public:
    // The name reaches generated CMake and generated QML, so it has to be
    // checked before anything is written. An empty name used to scaffold into
    // the current directory, and "My App" produced CMake that cannot parse.
    static bool isValidProjectName(const QString &name, QString *error = nullptr);

    struct Options {
        QString organization = QStringLiteral("dev.example");
        // Off by default. The generated workflow cannot install loom without a
        // published release, so scaffolding one by default gave every new
        // project a guaranteed red X on its first push.
        bool githubWorkflow = false;
    };

    static bool create(
        const QString &name, const QString &destination, const Options &options,
        QString *error = nullptr);
};
