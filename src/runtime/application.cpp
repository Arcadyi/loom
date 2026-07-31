#include <loom/application.h>
#include <loom/reloadcontroller.h>

#include <QByteArray>
#include <QHostAddress>

namespace loom {

void Application::connectDevelopmentRuntime()
{
    auto host = qEnvironmentVariable("LOOM_DEV_HOST", QStringLiteral("127.0.0.1"));
    const auto rawPort = qEnvironmentVariable("LOOM_DEV_PORT");
    const auto token = qEnvironmentVariable("LOOM_DEV_TOKEN");

    if (rawPort.isEmpty() && token.isEmpty())
        return;

    if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
        host = QStringLiteral("127.0.0.1");
    // A reload bundle is executable QML, so only ever accept one from this
    // machine. Without this the runtime would happily run code served by an
    // arbitrary remote host.
    const QHostAddress address(host);
    if (address.isNull() || !address.isLoopback()) {
        qWarning(
            "loom: refusing development host '%s'; only loopback is allowed",
            qUtf8Printable(host));
        return;
    }

    bool portValid = false;
    const auto port = rawPort.toUInt(&portValid);
    if (!portValid || port == 0 || port > 65535) {
        qWarning(
            "loom: LOOM_DEV_PORT '%s' is not a valid port; "
            "hot reload is disabled",
            qUtf8Printable(rawPort));
        return;
    }
    if (token.isEmpty()) {
        qWarning("loom: LOOM_DEV_TOKEN is not set; hot reload is disabled");
        return;
    }

    m_reloadController->connectToDevelopmentServer(
        host, static_cast<quint16>(port), token);
}

Application::Application(int &argc, char **argv)
    : m_application(argc, argv)
{
}

Application::~Application() = default;

void Application::setEngineInitializer(EngineInitializer initializer)
{
    m_initializer = std::move(initializer);
}

void Application::enableDevelopmentRuntime(const bool enabled)
{
    m_developmentRuntimeEnabled = enabled;
}

int Application::run(const QString &moduleUri, const QString &entryType)
{
    if (m_initializer)
        m_initializer(m_engine);

    m_reloadController = std::make_unique<ReloadController>(m_engine);
    if (!m_reloadController->load(moduleUri, entryType)) {
        // Previously a bare `return 1`: the application exited non-zero with no
        // output whatsoever, which is the least actionable failure there is.
        // The QML errors are already collected in lastError().
        qWarning(
            "loom: could not load %s from module %s:\n%s", qUtf8Printable(entryType),
            qUtf8Printable(moduleUri), qUtf8Printable(m_reloadController->lastError()));
        return 1;
    }

    if (m_developmentRuntimeEnabled)
        connectDevelopmentRuntime();

    return m_application.exec();
}

QGuiApplication &Application::guiApplication()
{
    return m_application;
}

QQmlApplicationEngine &Application::engine()
{
    return m_engine;
}

} // namespace loom
