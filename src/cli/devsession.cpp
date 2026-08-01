#include "devsession.h"

#include "buildrunner.h"
#include "commandline.h"

#include <QCoreApplication>
#include <QDir>
#include <QProcessEnvironment>
#include <QSocketNotifier>
#include <QTextStream>

#if defined(Q_OS_UNIX)
#include <csignal>
#include <unistd.h>
#endif

namespace {

// How long to wait for a polite terminate before killing.
constexpr int TerminateTimeoutMs = 3000;

void logEvent(const QString &message)
{
    QTextStream(stdout) << "[loom] " << message << Qt::endl;
}

void logProblem(const QString &message)
{
    QTextStream(stderr) << "[loom] " << message << Qt::endl;
}

#if defined(Q_OS_UNIX)
// Self-pipe. A signal handler may only touch async-signal-safe functions, so it
// writes one byte here and a QSocketNotifier turns that into an ordinary
// queued call on the Qt event loop.
int signalPipe[2] = {-1, -1};

extern "C" void writeSignalToPipe(int number)
{
    const auto byte = static_cast<char>(number);
    // Return value deliberately ignored: there is nothing safe to do about a
    // failed write from inside a signal handler.
    const auto ignored = ::write(signalPipe[1], &byte, 1);
    Q_UNUSED(ignored)
}
#endif

} // namespace

DevSession::DevSession(Configuration configuration, QObject *parent)
    : QObject(parent)
    , m_configuration(std::move(configuration))
    , m_server(
          m_configuration.projectRoot, m_configuration.application,
          m_configuration.buildDirectory, this)
{
    m_server.setDesignPath(m_configuration.designPath);
}

DevSession::~DevSession()
{
    // Never leave the child running: it holds the reload connection and, more
    // visibly, a window.
    stopApplication();
}

int DevSession::run(QString *error)
{
    connect(&m_server, &DevServer::logMessage, this, &logEvent);
    connect(
        &m_server, &DevServer::nativeFilesChanged, this,
        &DevSession::handleNativeFilesChanged);
    if (!m_server.start(error))
        return cli::Failure;

    m_process.setProcessChannelMode(QProcess::ForwardedChannels);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LOOM_DEV_HOST"), QStringLiteral("127.0.0.1"));
    environment.insert(QStringLiteral("LOOM_DEV_PORT"), QString::number(m_server.port()));
    environment.insert(QStringLiteral("LOOM_DEV_TOKEN"), m_server.token());
    m_process.setProcessEnvironment(environment);
    connect(&m_process, &QProcess::finished, this, &DevSession::handleProcessFinished);
    connect(&m_process, &QProcess::errorOccurred, this, &DevSession::handleProcessError);

    installSignalHandlers();

    m_process.start(m_configuration.executable, m_configuration.applicationArguments);
    if (!m_process.waitForStarted()) {
        if (error) {
            *error = QStringLiteral("could not start %1: %2")
                         .arg(m_configuration.executable, m_process.errorString());
        }
        return 127;
    }

    m_eventLoopRunning = true;
    const auto status = QCoreApplication::exec();
    m_eventLoopRunning = false;
    return status;
}

void DevSession::startApplication()
{
    m_process.start(m_configuration.executable, m_configuration.applicationArguments);
    // Not fatal here: errorOccurred(FailedToStart) arrives either way and is
    // what ends the session, so a slow start is not mistaken for a failed one.
    if (!m_process.waitForStarted())
        return;
    logEvent(QStringLiteral("application restarted"));
}

void DevSession::stopApplication()
{
    if (m_process.state() == QProcess::NotRunning)
        return;
    m_process.terminate();
    if (!m_process.waitForFinished(TerminateTimeoutMs)) {
        m_process.kill();
        m_process.waitForFinished(TerminateTimeoutMs);
    }
}

void DevSession::handleNativeFilesChanged()
{
    if (m_restarting || m_finishing)
        return;
    m_restarting = true;
    logEvent(QStringLiteral("native or build input changed; rebuilding"));
    stopApplication();

    const bool built = BuildRunner::configure(
                           m_configuration.projectRoot, m_configuration.buildDirectory,
                           m_configuration.buildConfiguration,
                           m_configuration.cmakeArguments, m_configuration.generator)
            == 0
        && BuildRunner::build(
               m_configuration.buildDirectory, m_configuration.application.target,
               m_configuration.buildConfiguration)
            == 0;
    m_restarting = false;
    if (!built) {
        logProblem(QStringLiteral("rebuild failed; waiting for another source change"));
        return;
    }
    reloadManifest();
    // Always, not just when the manifest changed: the bundled qmldir comes from
    // the build tree, so a CMake edit that adds a singleton changes what the
    // bundle must contain without touching any watched QML file.
    m_server.refreshBundle();
    startApplication();
}

void DevSession::reloadManifest()
{
    const auto manifestPath =
        QDir(m_configuration.projectRoot).filePath(QStringLiteral("loom.json"));
    ProjectManifest manifest;
    QString error;
    if (!ProjectManifest::load(manifestPath, manifest, &error)) {
        logProblem(QStringLiteral(
                       "loom.json could not be re-read (%1); keeping the "
                       "project layout this session started with")
                       .arg(error));
        return;
    }

    ApplicationDefinition application;
    if (!manifest.selectApplication(
            m_configuration.application.target, application, &error)) {
        logProblem(QStringLiteral(
                       "loom.json no longer describes %1 (%2); keeping the "
                       "project layout this session started with")
                       .arg(m_configuration.application.target, error));
        return;
    }

    m_configuration.application = application;
    m_server.setApplication(application);

    const auto designPath = manifest.resolvedDesignPath(manifestPath);
    if (designPath != m_configuration.designPath) {
        m_configuration.designPath = designPath;
        m_server.setDesignPath(designPath);
    }
}

void DevSession::handleProcessFinished(
    const int exitCode, const QProcess::ExitStatus status)
{
    // A restart terminates the process on purpose; that is not the application
    // exiting.
    if (m_restarting || m_finishing)
        return;
    finish(status == QProcess::NormalExit ? exitCode : 128);
}

void DevSession::handleProcessError(const QProcess::ProcessError error)
{
    if (m_finishing)
        return;
    if (error != QProcess::FailedToStart) {
        // Crashes arrive through finished() with CrashExit; the rest are
        // reportable but not fatal to the session.
        logProblem(
            QStringLiteral("application process error: %1").arg(m_process.errorString()));
        return;
    }
    // FailedToStart emits no finished() signal. Without this the session sat in
    // exec() forever with no application running and no nonzero exit.
    logProblem(QStringLiteral("could not start %1: %2")
                   .arg(m_configuration.executable, m_process.errorString()));
    finish(127);
}

void DevSession::finish(const int exitCode)
{
    if (m_finishing)
        return;
    m_finishing = true;
    stopApplication();
    if (m_eventLoopRunning)
        QCoreApplication::exit(exitCode);
}

void DevSession::installSignalHandlers()
{
#if defined(Q_OS_UNIX)
    if (::pipe(signalPipe) != 0) {
        logProblem(QStringLiteral(
            "could not install signal handlers; Ctrl-C may leave the application "
            "running"));
        return;
    }
    m_signalNotifier = new QSocketNotifier(signalPipe[0], QSocketNotifier::Read, this);
    connect(
        m_signalNotifier, &QSocketNotifier::activated, this, &DevSession::handleSignal);

    struct sigaction action{};
    action.sa_handler = writeSignalToPipe;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    ::sigaction(SIGINT, &action, nullptr);
    ::sigaction(SIGTERM, &action, nullptr);
#else
    // Interactive Ctrl-C reaches the whole process group, which masks the
    // problem; a signal sent to loom dev alone would not be handled here.
    logEvent(QStringLiteral(
        "signal handling is not implemented on this platform; stop the session "
        "with Ctrl-C"));
#endif
}

void DevSession::handleSignal()
{
#if defined(Q_OS_UNIX)
    char number = 0;
    const auto read = ::read(signalPipe[0], &number, 1);
    if (read != 1)
        return;
    logEvent(QStringLiteral("received signal %1; stopping the application")
                 .arg(static_cast<int>(number)));
    // 128 + signal number is the shell convention for "killed by this signal".
    finish(128 + static_cast<int>(number));
#endif
}
