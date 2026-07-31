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
        // Off by default. The generated workflow cannot install respin without a
        // published release, so scaffolding one by default gave every new
        // project a guaranteed red X on its first push.
        bool githubWorkflow = false;
        // Pre-wires the loom styling library: the loom template overlay
        // replaces CMakeLists.txt and qml/Main.qml with versions that
        // find_package(loom), link it, and style the starter UI through it.
        bool loom = false;
    };

    static bool create(
        const QString &name, const QString &destination, const Options &options,
        QString *error = nullptr);
};
