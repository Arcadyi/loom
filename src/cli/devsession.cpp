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

QString shellQuote(QString value)
{
    return QLatin1Char('\'') + value.replace(QLatin1Char('\''), QStringLiteral("'\\''"))
        + QLatin1Char('\'');
}

QString remoteHost(const QJsonObject &profile)
{
    const QString host = profile.value(QStringLiteral("host")).toString();
    const QString user = profile.value(QStringLiteral("user")).toString();
    return user.isEmpty() ? host : user + QLatin1Char('@') + host;
}

QStringList adbPrefix(const QJsonObject &options)
{
    const QString device = options.value(QStringLiteral("device")).toString();
    return device.isEmpty() ? QStringList{} : QStringList{QStringLiteral("-s"), device};
}

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
    const bool physicalIos = m_configuration.platform == QLatin1String("ios")
        && m_configuration.platformOptions.value(QStringLiteral("destination"))
                .toString(QStringLiteral("simulator"))
            == QLatin1String("device");
    m_server.setRemoteAccess(physicalIos);
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
    if (m_configuration.platform == QLatin1String("desktop")) {
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("LOOM_DEV_HOST"), QStringLiteral("127.0.0.1"));
        environment.insert(
            QStringLiteral("LOOM_DEV_PORT"), QString::number(m_server.port()));
        environment.insert(QStringLiteral("LOOM_DEV_TOKEN"), m_server.token());
        m_process.setProcessEnvironment(environment);
    }
    connect(&m_process, &QProcess::finished, this, &DevSession::handleProcessFinished);
    connect(&m_process, &QProcess::errorOccurred, this, &DevSession::handleProcessError);

    installSignalHandlers();

    if (m_configuration.platform == QLatin1String("desktop")) {
        m_process.start(m_configuration.executable, m_configuration.applicationArguments);
        if (!m_process.waitForStarted()) {
            if (error) {
                *error = QStringLiteral("could not start %1: %2")
                             .arg(m_configuration.executable, m_process.errorString());
            }
            return 127;
        }
    } else if (!startRemoteApplication(error)) {
        return cli::Failure;
    }

    m_eventLoopRunning = true;
    const auto status = QCoreApplication::exec();
    m_eventLoopRunning = false;
    return status;
}

void DevSession::startApplication()
{
    if (m_configuration.platform != QLatin1String("desktop")) {
        QString error;
        if (!startRemoteApplication(&error)) {
            logProblem(error);
            finish(cli::Failure);
        }
        return;
    }
    m_process.start(m_configuration.executable, m_configuration.applicationArguments);
    // Not fatal here: errorOccurred(FailedToStart) arrives either way and is
    // what ends the session, so a slow start is not mistaken for a failed one.
    if (!m_process.waitForStarted())
        return;
    logEvent(QStringLiteral("application restarted"));
}

bool DevSession::deployRemoteArtifact(QString *error)
{
    if (m_configuration.platform == QLatin1String("android")) {
        QStringList arguments = adbPrefix(m_configuration.platformOptions);
        arguments.append(
            {QStringLiteral("install"), QStringLiteral("-r"),
             m_configuration.executable});
        const int status = BuildRunner::run(QStringLiteral("adb"), arguments);
        if (status != 0 && error)
            *error = QStringLiteral("adb could not install %1")
                         .arg(m_configuration.executable);
        return status == 0;
    }
    if (m_configuration.platform == QLatin1String("ios")) {
        const QString destination =
            m_configuration.platformOptions.value(QStringLiteral("destination"))
                .toString(QStringLiteral("simulator"));
        const QString device =
            m_configuration.platformOptions.value(QStringLiteral("device"))
                .toString(QStringLiteral("booted"));
        QStringList arguments;
        if (destination == QLatin1String("simulator")) {
            arguments = {
                QStringLiteral("simctl"), QStringLiteral("install"), device,
                m_configuration.executable};
        } else {
            arguments = {QStringLiteral("devicectl"), QStringLiteral("device"),
                         QStringLiteral("install"),   QStringLiteral("app"),
                         QStringLiteral("--device"),  device,
                         m_configuration.executable};
        }
        const int status = BuildRunner::run(QStringLiteral("xcrun"), arguments);
        if (status != 0 && error)
            *error = QStringLiteral("xcrun could not install %1")
                         .arg(m_configuration.executable);
        return status == 0;
    }
    if (m_configuration.platform == QLatin1String("embedded")) {
        const QString remoteDir =
            m_configuration.embeddedProfile.value(QStringLiteral("remoteDir")).toString();
        QStringList ssh;
        const int port =
            m_configuration.embeddedProfile.value(QStringLiteral("port")).toInt(22);
        if (port != 22)
            ssh.append({QStringLiteral("-p"), QString::number(port)});
        ssh.append(
            {remoteHost(m_configuration.embeddedProfile),
             QStringLiteral("mkdir -p -- %1").arg(shellQuote(remoteDir))});
        if (BuildRunner::run(QStringLiteral("ssh"), ssh) != 0)
            return false;
        QStringList rsync{QStringLiteral("-az")};
        if (port != 22)
            rsync.append({QStringLiteral("-e"), QStringLiteral("ssh -p %1").arg(port)});
        rsync.append(
            {m_configuration.executable,
             remoteHost(m_configuration.embeddedProfile) + QLatin1Char(':') + remoteDir
                 + QLatin1Char('/')});
        const int status = BuildRunner::run(QStringLiteral("rsync"), rsync);
        if (status != 0 && error)
            *error = QStringLiteral("rsync could not transfer the embedded executable");
        return status == 0;
    }
    if (error)
        *error =
            QStringLiteral("unknown remote platform %1").arg(m_configuration.platform);
    return false;
}

bool DevSession::startRemoteApplication(QString *error)
{
    if (!deployRemoteArtifact(error))
        return false;
    const QString port = QString::number(m_server.port());
    const bool physicalIos = m_configuration.platform == QLatin1String("ios")
        && m_configuration.platformOptions.value(QStringLiteral("destination"))
                .toString(QStringLiteral("simulator"))
            == QLatin1String("device");
    const QString developmentHost = physicalIos
        ? m_configuration.platformOptions.value(QStringLiteral("host")).toString()
        : QStringLiteral("127.0.0.1");
    if (physicalIos && developmentHost.isEmpty()) {
        if (error)
            *error =
                QStringLiteral("physical iOS development requires platforms.ios.host");
        return false;
    }
    QStringList runtimeArguments{
        QStringLiteral("--loom-dev-host=") + developmentHost,
        QStringLiteral("--loom-dev-port=") + port,
        QStringLiteral("--loom-dev-token=") + m_server.token(),
    };
    if (physicalIos)
        runtimeArguments.append(QStringLiteral("--loom-dev-allow-remote"));
    if (m_configuration.platform == QLatin1String("android")) {
        QStringList prefix = adbPrefix(m_configuration.platformOptions);
        QStringList reverse = prefix;
        reverse.append(
            {QStringLiteral("reverse"), QStringLiteral("tcp:") + port,
             QStringLiteral("tcp:") + port});
        if (BuildRunner::run(QStringLiteral("adb"), reverse) != 0)
            return false;
        QStringList stop = prefix;
        stop.append(
            {QStringLiteral("shell"), QStringLiteral("am"), QStringLiteral("force-stop"),
             m_configuration.application.id});
        BuildRunner::run(QStringLiteral("adb"), stop);
        QStringList allArguments =
            runtimeArguments + m_configuration.applicationArguments;
        QStringList launch = prefix;
        launch.append(
            {QStringLiteral("shell"), QStringLiteral("am"), QStringLiteral("start"),
             QStringLiteral("-n"),
             m_configuration.application.id
                 + QStringLiteral("/org.qtproject.qt.android.bindings.QtActivity"),
             QStringLiteral("--es"), QStringLiteral("applicationArguments"),
             allArguments.join(QLatin1Char(' '))});
        const int status = BuildRunner::run(QStringLiteral("adb"), launch);
        if (status != 0) {
            if (error)
                *error = QStringLiteral("adb could not launch %1")
                             .arg(m_configuration.application.id);
            return false;
        }
        m_remoteDetached = true;
        logEvent(QStringLiteral("Android application installed and launched"));
        return true;
    }
    if (m_configuration.platform == QLatin1String("ios")) {
        const QString destination =
            m_configuration.platformOptions.value(QStringLiteral("destination"))
                .toString(QStringLiteral("simulator"));
        const QString device =
            m_configuration.platformOptions.value(QStringLiteral("device"))
                .toString(QStringLiteral("booted"));
        QStringList arguments;
        if (destination == QLatin1String("simulator")) {
            arguments = {
                QStringLiteral("simctl"), QStringLiteral("launch"),
                QStringLiteral("--console"), device, m_configuration.application.id};
        } else {
            arguments = {
                QStringLiteral("devicectl"),
                QStringLiteral("device"),
                QStringLiteral("process"),
                QStringLiteral("launch"),
                QStringLiteral("--console"),
                QStringLiteral("--device"),
                device,
                m_configuration.application.id,
                QStringLiteral("--")};
        }
        arguments.append(runtimeArguments);
        arguments.append(m_configuration.applicationArguments);
        m_process.start(QStringLiteral("xcrun"), arguments);
        if (!m_process.waitForStarted()) {
            if (error)
                *error = QStringLiteral("could not launch iOS application: %1")
                             .arg(m_process.errorString());
            return false;
        }
        return true;
    }
    if (m_configuration.platform == QLatin1String("embedded")) {
        const int sshPort =
            m_configuration.embeddedProfile.value(QStringLiteral("port")).toInt(22);
        QStringList arguments;
        if (sshPort != 22)
            arguments.append({QStringLiteral("-p"), QString::number(sshPort)});
        arguments.append(
            {QStringLiteral("-R"), QStringLiteral("%1:127.0.0.1:%1").arg(port),
             remoteHost(m_configuration.embeddedProfile)});
        const QString remoteDir =
            m_configuration.embeddedProfile.value(QStringLiteral("remoteDir")).toString();
        QString command =
            QStringLiteral(
                "cd %1 && env LOOM_DEV_HOST=127.0.0.1 "
                "LOOM_DEV_PORT=%2 LOOM_DEV_TOKEN=%3 ")
                .arg(shellQuote(remoteDir), port, shellQuote(m_server.token()));
        const QJsonObject environment =
            m_configuration.embeddedProfile.value(QStringLiteral("environment"))
                .toObject();
        for (auto it = environment.constBegin(); it != environment.constEnd(); ++it)
            command += it.key() + QLatin1Char('=') + shellQuote(it->toString())
                + QLatin1Char(' ');
        const QString launch =
            m_configuration.embeddedProfile.value(QStringLiteral("launchCommand"))
                .toString(
                    QStringLiteral("./")
                    + QFileInfo(m_configuration.executable).fileName());
        command += launch;
        for (const QString &argument : m_configuration.applicationArguments)
            command += QLatin1Char(' ') + shellQuote(argument);
        arguments.append(command);
        m_process.start(QStringLiteral("ssh"), arguments);
        if (!m_process.waitForStarted()) {
            if (error)
                *error = QStringLiteral("could not launch embedded application: %1")
                             .arg(m_process.errorString());
            return false;
        }
        return true;
    }
    return false;
}

void DevSession::stopApplication()
{
    if (m_remoteDetached && m_configuration.platform == QLatin1String("android")) {
        QStringList arguments = adbPrefix(m_configuration.platformOptions);
        arguments.append(
            {QStringLiteral("shell"), QStringLiteral("am"), QStringLiteral("force-stop"),
             m_configuration.application.id});
        BuildRunner::run(QStringLiteral("adb"), arguments);
        m_remoteDetached = false;
        return;
    }
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
    m_configuration.platformOptions =
        application.platformOptions.value(m_configuration.platform).toObject();
    if (m_configuration.platform == QLatin1String("embedded")) {
        const QString profile =
            m_configuration.platformOptions.value(QStringLiteral("profile")).toString();
        m_configuration.embeddedProfile =
            manifest.embeddedProfiles().value(profile).toObject();
    }
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
