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

    // What `loom add` can generate into an existing project. Each maps to one
    // template and one conventional subdirectory of the QML root.
    enum class SourceKind {
        Page,
        Component,
    };

    // A QML type name, which is what the file is named after. Must start with
    // an upper-case letter: QML resolves a component from its file name, so a
    // lower-case one is not addressable as a type at all.
    static bool isValidTypeName(const QString &name, QString *error = nullptr);

    // Writes one source file under `qmlRoot` and reports where it landed.
    //
    // Deliberately never touches CMake. loom_add_application globs QML_ROOT
    // with CONFIGURE_DEPENDS, so a new file is picked up on the next configure
    // without anything being registered -- which is what keeps this a pure
    // creation with nothing to undo if it is unwanted.
    //
    // Refuses rather than overwrites: this runs against a project someone is
    // working in.
    static bool addSource(
        SourceKind kind, const QString &typeName, const QString &qmlRoot,
        QString *createdPath = nullptr, QString *error = nullptr);
};
