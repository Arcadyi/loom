#pragma once

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QString>

#include <functional>
#include <memory>

namespace respin {

class ReloadController;

/// Entry point for a respin application: owns a QGuiApplication and a
/// QQmlApplicationEngine, loads the root scene, and optionally connects to a
/// development server for hot reload.
///
/// A generated main.cpp is the whole of the API most applications need:
///
/// \code
/// respin::Application application(argc, argv);
/// #ifndef NDEBUG
///     application.enableDevelopmentRuntime();
/// #endif
/// return application.run(
///     QStringLiteral(RESPIN_APP_URI), QStringLiteral(RESPIN_APP_ENTRY));
/// \endcode
///
/// RESPIN_APP_URI and RESPIN_APP_ENTRY are defined by respin_enable_hot_reload,
/// so the module URI has a single definition rather than one in CMake and
/// another in C++.
///
/// \sa docs/runtime-api.md
class Application final {
public:
    /// Called with the engine before the scene is loaded.
    using EngineInitializer = std::function<void(QQmlApplicationEngine &)>;

    Application(int &argc, char **argv);
    ~Application();

    Application(const Application &) = delete;
    Application &operator=(const Application &) = delete;

    /// Registers a callback run against the engine immediately before the root
    /// scene is created. This is where C++ types, context properties and import
    /// paths belong.
    ///
    /// Anything registered here survives hot reload: only the QML scene is
    /// rebuilt, while the engine and every C++ object outlive it. Changing the
    /// C++ itself needs a rebuild, which `respin dev` performs automatically.
    void setEngineInitializer(EngineInitializer initializer);

    /// Allows the runtime to connect to a development server. Without this the
    /// application never opens a socket, whatever RESPIN_DEV_* says.
    ///
    /// The generated main.cpp gates this on `#ifndef NDEBUG`, so a Release,
    /// RelWithDebInfo or MinSizeRel build has no reload path compiled in at all.
    void enableDevelopmentRuntime(bool enabled = true);

    /// Loads \a entryType from \a moduleUri, connects the development runtime if
    /// it was enabled, and runs the event loop.
    ///
    /// \return the event loop's exit code, or 1 if the scene could not be
    ///         loaded, in which case the QML errors are printed to stderr.
    int run(const QString &moduleUri, const QString &entryType);

    /// The owned QGuiApplication, for anything this wrapper does not cover.
    QGuiApplication &guiApplication();

    /// The owned engine. Prefer setEngineInitializer() for setup that must
    /// happen before the scene loads.
    QQmlApplicationEngine &engine();

private:
    void connectDevelopmentRuntime();

    QGuiApplication m_application;
    QQmlApplicationEngine m_engine;
    EngineInitializer m_initializer;
    std::unique_ptr<ReloadController> m_reloadController;
    bool m_developmentRuntimeEnabled = false;
};

} // namespace respin
