#include <loom/protocol.h>
#include <loom/reloadcontroller.h>

#include "statecapture.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#if defined(Q_OS_UNIX)
#include <cerrno>
// <signal.h>, not <csignal>: kill() is POSIX and is not declared by the C++ header.
#include <signal.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace {

// Written into a bundle directory once every file is on disk, and the last
// thing written before the directory is renamed into place. A directory without
// it is a partial write from a run that died mid-stage, and is never loadable.
constexpr auto CompleteMarkerName = ".loom-complete";

QString errorsToString(const QList<QQmlError> &errors)
{
    QStringList messages;
    for (const auto &error : errors)
        messages.append(error.toString());
    return messages.join(QLatin1Char('\n'));
}

// Every caller used to discard the result, so a cache directory that could not
// be removed produced no diagnostic anywhere and the next reload failed for an
// unrelated-looking reason.
bool removePath(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return true;
    const bool removed =
        info.isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
    if (!removed)
        qWarning(
            "loom: could not remove %s from the bundle cache", qUtf8Printable(path));
    return removed;
}

bool isCompleteBundle(const QString &path)
{
    if (path.isEmpty())
        return false;
    return QFileInfo::exists(path + QLatin1Char('/') + QLatin1String(CompleteMarkerName));
}

bool processIsRunning(const qint64 pid)
{
#if defined(Q_OS_UNIX)
    // EPERM means the process exists but belongs to another user.
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#elif defined(Q_OS_WIN)
    HANDLE process =
        ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process)
        return false;
    ::CloseHandle(process);
    return true;
#else
    Q_UNUSED(pid)
    // Unknown platform: never claim a directory is abandoned.
    return true;
#endif
}

// Bundle roots are named "<pid>-XXXXXX". Anything whose owning process is gone
// is this run's to clean up, as is anything not following that scheme at all --
// staging leftovers, and the flat "<cache>/loom/bundles/<id>" directories
// written before the cache was scoped per process.
void sweepAbandonedBundleRoots(const QString &base)
{
    static const QRegularExpression ownerPattern(QStringLiteral("^(\\d+)-"));
    const QDir directory(base);
    const auto entries = directory.entryInfoList(
        QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const auto &entry : entries) {
        const auto match = ownerPattern.match(entry.fileName());
        if (match.hasMatch() && entry.isDir()) {
            bool parsed = false;
            const auto pid = match.captured(1).toLongLong(&parsed);
            if (parsed && pid > 0 && processIsRunning(pid))
                continue;
        }
        removePath(entry.absoluteFilePath());
    }
}

} // namespace

namespace loom {

ReloadController::ReloadController(QQmlApplicationEngine &engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
{
}

ReloadController::~ReloadController()
{
    // loadUrl() creates the root scene without a parent, so nothing else owns
    // it. The engine outlives this controller, so deleting here is safe and
    // keeps the last scene from leaking at shutdown.
    delete m_rootObject;
}

bool ReloadController::load(const QString &moduleUri, const QString &entryType)
{
    m_moduleUri = moduleUri;
    m_entryType = entryType;
    QString error;
    if (!loadUrl(resourceEntryUrl(), {}, &error)) {
        m_lastError = error;
        emit reloadFailed(error);
        return false;
    }
    return true;
}

void ReloadController::connectToDevelopmentServer(
    const QString &host, const quint16 port, const QString &token)
{
    m_token = token;
    m_host = host;
    m_port = port;
    m_reconnectAttempts = 0;

    // Engine warnings are forwarded to the dev server so they appear in the
    // terminal running `loom dev` rather than only in the application's own
    // output. They never trigger a rollback -- see the note on handleEngineWarnings.
    if (!m_warningsConnected) {
        connect(
            &m_engine, &QQmlEngine::warnings, this,
            [this](const QList<QQmlError> &warnings) {
                if (!warnings.isEmpty())
                    reportEngineWarnings(errorsToString(warnings));
            });
        m_warningsConnected = true;
    }
    openConnection();
}

void ReloadController::openConnection()
{
    if (m_socket)
        m_socket->deleteLater();
    m_socket = new QTcpSocket(this);
    // A reconnect resumes mid-frame otherwise: the buffer still holds the tail
    // of whatever the previous connection was carrying when it dropped.
    m_readBuffer.clear();

    if (!m_silenceWatchdog) {
        m_silenceWatchdog = new QTimer(this);
        m_silenceWatchdog->setSingleShot(true);
        m_silenceWatchdog->setInterval(ServerSilenceTimeoutMs);
        connect(
            m_silenceWatchdog, &QTimer::timeout, this,
            &ReloadController::handleServerSilence);
    }
    m_silenceWatchdog->stop();

    connect(m_socket, &QTcpSocket::readyRead, this, &ReloadController::handleSocketData);
    connect(m_socket, &QTcpSocket::connected, this, [this] {
        m_reconnectAttempts = 0;
        m_silenceWatchdog->start();
        QJsonObject hello{
            {QStringLiteral("version"), ProtocolVersion},
            {QStringLiteral("token"), m_token},
            {QStringLiteral("applicationId"), QCoreApplication::applicationName()},
            {QStringLiteral("currentBundle"), m_activeBundleId},
        };
        m_socket->write(encodeFrame(
            MessageType::Hello, QJsonDocument(hello).toJson(QJsonDocument::Compact)));
    });
    // Without these, a refused connection or a rejected token killed hot reload
    // silently and permanently: nothing observed the failure and nothing retried.
    connect(
        m_socket, &QTcpSocket::errorOccurred, this,
        [this](const QAbstractSocket::SocketError) {
            scheduleReconnect(m_socket ? m_socket->errorString() : QString());
        });
    connect(m_socket, &QTcpSocket::disconnected, this, [this] {
        scheduleReconnect(QStringLiteral("connection closed"));
    });

    m_socket->connectToHost(m_host, m_port);
}

// A half-open connection looks identical to an idle one, so the runtime would
// sit forever believing hot reload was live. Aborting hands the socket to the
// existing bounded reconnect.
void ReloadController::handleServerSilence()
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;
    qWarning(
        "loom: no word from the development server for %d seconds; reconnecting",
        ServerSilenceTimeoutMs / 1000);
    m_socket->abort();
    scheduleReconnect(QStringLiteral("server stopped responding"));
}

void ReloadController::scheduleReconnect(const QString &reason)
{
    if (m_silenceWatchdog)
        m_silenceWatchdog->stop();
    if (m_reconnectAttempts >= MaximumReconnectAttempts) {
        if (m_reconnectAttempts == MaximumReconnectAttempts) {
            ++m_reconnectAttempts; // report once, then stay quiet
            qWarning(
                "loom: giving up on the development server at %s:%u after %d "
                "attempts (%s); hot reload is off for this run",
                qUtf8Printable(m_host), m_port, MaximumReconnectAttempts,
                qUtf8Printable(reason));
        }
        return;
    }

    // Exponential, capped: a dev server that is restarting comes back in
    // seconds, and one that is gone should not be probed forever.
    const int delay = qMin(
        InitialReconnectDelayMs * (1 << m_reconnectAttempts), MaximumReconnectDelayMs);
    ++m_reconnectAttempts;
    if (m_reconnectAttempts == 1) {
        qWarning(
            "loom: lost the development server at %s:%u (%s); retrying",
            qUtf8Printable(m_host), m_port, qUtf8Printable(reason));
    }
    QTimer::singleShot(delay, this, &ReloadController::openConnection);
}

// Reported, never acted on. Rollback only knows how to undo a scene that failed
// to construct; a warning means the scene is up and something in it misbehaved,
// and treating that as fatal would roll back working reloads. "Last known good"
// therefore means "constructed successfully", not "runs without warnings".
void ReloadController::reportEngineWarnings(const QString &text)
{
    qWarning("loom: QML warning:\n%s", qUtf8Printable(text));
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;
    const QJsonObject payload{{QStringLiteral("message"), text}};
    m_socket->write(encodeFrame(
        MessageType::Error, QJsonDocument(payload).toJson(QJsonDocument::Compact)));
}

QObject *ReloadController::rootObject() const
{
    return m_rootObject;
}

QString ReloadController::lastError() const
{
    return m_lastError;
}

bool ReloadController::loadUrl(const QUrl &url, const QVariant &state, QString *error)
{
    QQmlComponent component(&m_engine);
    component.loadUrl(url, QQmlComponent::PreferSynchronous);
    if (component.isError()) {
        if (error)
            *error = errorsToString(component.errors());
        return false;
    }

    QObject *candidate = component.create(m_engine.rootContext());
    if (!candidate) {
        if (error)
            *error = errorsToString(component.errors());
        return false;
    }

    m_rootObject = candidate;
    restoreState(candidate, state);
    return true;
}

// Bundle directories are private to this ReloadController. Two instances of the
// same application previously shared a deterministic
// "<cache>/loom/bundles/<id>" path and called removeRecursively() on each
// other's live directories mid-write.
bool ReloadController::ensureCacheDirectory(QString *error)
{
    if (m_cacheDirectory)
        return true;

    const auto base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/loom/bundles");
    if (!QDir().mkpath(base)) {
        if (error)
            *error = QStringLiteral("Could not create loom bundle cache");
        return false;
    }

    // QTemporaryDir cleans up on a normal exit; this covers the kills and
    // crashes that a development loop produces constantly.
    sweepAbandonedBundleRoots(base);

    // The pid prefix is what makes the sweep above possible: a QTemporaryDir
    // name alone says nothing about who owns it.
    auto directory = std::make_unique<QTemporaryDir>(
        base + QLatin1Char('/') + QString::number(QCoreApplication::applicationPid())
        + QStringLiteral("-XXXXXX"));
    if (!directory->isValid()) {
        if (error) {
            *error = QStringLiteral("Could not create loom bundle cache: %1")
                         .arg(directory->errorString());
        }
        return false;
    }
    m_cacheDirectory = std::move(directory);
    return true;
}

// Writes the bundle to `destination` through a staging directory, so
// `destination` either does not exist or is complete -- it is never observed
// half-written, and an interrupted run leaves nothing loadable behind.
bool ReloadController::materializeBundle(
    const Bundle &bundle, const QString &destination, QString *error)
{
    // Only reached when the destination is absent or incomplete, so whatever is
    // there is a partial write.
    removePath(destination);

    // Staged as a sibling of the destination so activation is a rename within
    // one filesystem. QTemporaryDir removes it on every early return below.
    QTemporaryDir staging(m_cacheDirectory->path() + QStringLiteral("/.staging-XXXXXX"));
    if (!staging.isValid()) {
        if (error) {
            *error = QStringLiteral("Could not create bundle staging directory: %1")
                         .arg(staging.errorString());
        }
        return false;
    }

    for (const auto &file : bundle.files) {
        const auto absolutePath = staging.path() + QLatin1Char('/') + file.path;
        if (!QDir().mkpath(QFileInfo(absolutePath).absolutePath())) {
            if (error)
                *error =
                    QStringLiteral("Could not create directory for %1").arg(file.path);
            return false;
        }
        QSaveFile output(absolutePath);
        if (!output.open(QIODevice::WriteOnly)
            || output.write(file.contents) != file.contents.size() || !output.commit()) {
            if (error)
                *error = QStringLiteral("Could not stage %1").arg(file.path);
            return false;
        }
    }

    QSaveFile marker(
        staging.path() + QLatin1Char('/') + QLatin1String(CompleteMarkerName));
    if (!marker.open(QIODevice::WriteOnly) || !marker.commit()) {
        if (error)
            *error = QStringLiteral("Could not mark the staged bundle complete");
        return false;
    }

    staging.setAutoRemove(false);
    if (!QDir().rename(staging.path(), destination)) {
        staging.setAutoRemove(true);
        if (error)
            *error = QStringLiteral("Could not activate staged bundle");
        return false;
    }
    return true;
}

QUrl ReloadController::bundleEntryUrl(const QString &directory) const
{
    const auto uriPath = QString(m_moduleUri).replace(QLatin1Char('.'), QLatin1Char('/'));
    return QUrl::fromLocalFile(
        directory + QStringLiteral("/qt/qml/") + uriPath + QLatin1Char('/') + m_entryType
        + QStringLiteral(".qml"));
}

bool ReloadController::applyBundle(const QByteArray &payload, QString *error)
{
    Bundle bundle;
    if (!decodeBundle(payload, bundle, error))
        return false;

    // Bundle ids are content hashes, so a matching id is the scene already
    // running. Re-applying it used to delete the live bundle directory while
    // m_activeBundleDirectory still named it, after which every rollback loaded
    // a nonexistent file and the scene stayed empty for the rest of the session.
    // The dev server resends the current bundle on every reconnect, so this is
    // an ordinary path, not an edge case.
    if (!bundle.id.isEmpty() && bundle.id == m_activeBundleId && m_rootObject
        && isCompleteBundle(m_activeBundleDirectory)) {
        emit sceneReloaded(bundle.id);
        return true;
    }

    if (!ensureCacheDirectory(error))
        return false;

    const auto destination = m_cacheDirectory->path() + QLatin1Char('/') + bundle.id;
    // Content-addressed, so an existing complete directory holds exactly these
    // files and re-staging would only rewrite them.
    if (!isCompleteBundle(destination)
        && !materializeBundle(bundle, destination, error)) {
        return false;
    }

    // Compile the incoming scene while the running one is still untouched. A
    // bundle that does not compile -- the common case when someone saves a QML
    // file mid-edit -- then costs nothing: no teardown, no cleared engine
    // caches, no rollback, and the live scene never flickers.
    const auto candidateUrl = bundleEntryUrl(destination);
    {
        QQmlComponent probe(&m_engine);
        probe.loadUrl(candidateUrl, QQmlComponent::PreferSynchronous);
        if (probe.isError()) {
            if (error)
                *error = errorsToString(probe.errors());
            if (destination != m_activeBundleDirectory
                && destination != m_previousBundleDirectory) {
                removePath(destination);
            }
            return false;
        }
    }

    const auto state = saveState();
    const auto oldRoot = m_rootObject;
    m_rootObject.clear();
    delete oldRoot;
    m_engine.clearSingletons();
    m_engine.clearComponentCache();

    QString candidateError;
    if (!loadUrl(candidateUrl, state, &candidateError)) {
        m_engine.clearSingletons();
        m_engine.clearComponentCache();
        const QUrl rollbackUrl = m_activeBundleDirectory.isEmpty()
            ? resourceEntryUrl()
            : bundleEntryUrl(m_activeBundleDirectory);
        QString rollbackError;
        loadUrl(rollbackUrl, state, &rollbackError);
        if (error) {
            *error = rollbackError.isEmpty()
                ? candidateError
                : candidateError + QStringLiteral("\nRollback failed: ") + rollbackError;
        }
        // Only a failed candidate is safe to delete. When the incoming bundle
        // resolves to the directory the scene just rolled back into, deleting
        // it would destroy the last known good scene.
        if (destination != m_activeBundleDirectory
            && destination != m_previousBundleDirectory) {
            removePath(destination);
        }
        return false;
    }

    const auto superseded = m_activeBundleDirectory;
    m_previousBundleDirectory = superseded;
    m_activeBundleDirectory = destination;
    m_activeBundleId = bundle.id;
    if (!superseded.isEmpty() && superseded != destination)
        removePath(superseded);
    emit sceneReloaded(bundle.id);
    return true;
}

QVariant ReloadController::saveState() const
{
    if (!m_rootObject)
        return {};

    // Two halves in one envelope: every id-bearing object's declared
    // properties, captured with no cooperation from the scene, plus whatever
    // the root's loomSaveState() hook chose to return. The hook stays
    // because automatic capture deliberately skips bound properties and
    // objects with no id, and because a scene may want to carry something that
    // is not a property at all.
    QVariantMap envelope;

    const QVariantMap properties = captureSceneState(m_rootObject);
    if (!properties.isEmpty())
        envelope.insert(QStringLiteral("properties"), properties);

    if (m_rootObject->metaObject()->indexOfMethod("loomSaveState()") >= 0) {
        QVariant hookState;
        if (QMetaObject::invokeMethod(
                m_rootObject, "loomSaveState", Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, hookState))) {
            if (hookState.metaType() == QMetaType::fromType<QJSValue>())
                hookState = hookState.value<QJSValue>().toVariant();
            if (QJsonValue::fromVariant(hookState).isUndefined()) {
                // Only the hook's half is dropped. The captured properties owe
                // it nothing and are still good.
                qWarning(
                    "loom: loomSaveState() returned a value that is not "
                    "JSON-compatible; that hook's state was discarded");
            } else {
                envelope.insert(QStringLiteral("hook"), hookState);
            }
        }
    }

    if (envelope.isEmpty())
        return {};

    const auto jsonValue = QJsonValue::fromVariant(envelope);
    if (jsonValue.isUndefined())
        return {};
    if (QJsonDocument(jsonValue.toObject()).toJson(QJsonDocument::Compact).size()
        > MaximumStateSize) {
        qWarning(
            "loom: scene state came to more than %lld bytes; it was discarded",
            static_cast<long long>(MaximumStateSize));
        return {};
    }
    // Return the JSON-normalized value rather than the original variant. The
    // original may hold QML QObject pointers, which the caller carries across
    // the scene teardown that follows and would hand back to loomRestoreState
    // as dangling pointers.
    return jsonValue.toVariant();
}

void ReloadController::restoreState(QObject *root, const QVariant &state)
{
    if (!root || !state.isValid())
        return;

    const QVariantMap envelope = state.toMap();
    // Captured properties first, hook second: an explicit hook is the scene
    // author describing something the generic capture could not work out, so
    // it gets the last word wherever the two overlap.
    applySceneState(root, envelope.value(QStringLiteral("properties")).toMap());

    const QVariant hookState = envelope.value(QStringLiteral("hook"));
    if (!hookState.isValid())
        return;

    bool hasRestoreMethod = false;
    const auto *metaObject = root->metaObject();
    // Scan from 0, not methodOffset(), so a loomRestoreState inherited from a
    // base QML type is still found.
    for (int index = 0; index < metaObject->methodCount(); ++index) {
        if (metaObject->method(index).name() == QByteArrayLiteral("loomRestoreState")) {
            hasRestoreMethod = true;
            break;
        }
    }
    if (!hasRestoreMethod)
        return;
    QMetaObject::invokeMethod(
        root, "loomRestoreState", Qt::DirectConnection, Q_ARG(QVariant, hookState));
}

void ReloadController::handleSocketData()
{
    // sender(), not m_socket: a socket replaced by a reconnect can still have a
    // queued readyRead in flight, and servicing it through m_socket would read
    // the new connection's data into the old connection's context.
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket || socket != m_socket)
        return;

    m_readBuffer.append(socket->readAll());
    if (m_silenceWatchdog)
        m_silenceWatchdog->start();
    while (true) {
        Frame frame;
        QString frameError;
        if (!takeFrame(m_readBuffer, frame, &frameError)) {
            // A set error is a framing error. There is no resync marker, so the
            // only correct move is to drop the connection and start over.
            if (!frameError.isEmpty()) {
                qWarning(
                    "loom: development server sent an unreadable frame (%s); "
                    "reconnecting",
                    qUtf8Printable(frameError));
                socket->abort();
            }
            break;
        }
        if (frame.type == MessageType::Bundle) {
            QString error;
            const bool success = applyBundle(frame.payload, &error);
            m_lastError = error;
            sendResult(success, success ? QStringLiteral("Reloaded") : error);
            if (!success)
                emit reloadFailed(error);
        } else if (frame.type == MessageType::Ping) {
            socket->write(encodeFrame(MessageType::Ping, {}));
        } else if (frame.type == MessageType::Error) {
            const auto payload = QJsonDocument::fromJson(frame.payload).object();
            const auto message = payload.value(QStringLiteral("message")).toString();
            m_lastError = message;
            qWarning("loom: %s", qUtf8Printable(message));
            emit reloadFailed(message);
        }
    }
}

void ReloadController::sendResult(const bool success, const QString &message)
{
    if (!m_socket)
        return;
    const QJsonObject result{
        {QStringLiteral("success"), success},
        {QStringLiteral("bundleId"), m_activeBundleId},
        {QStringLiteral("message"), message},
    };
    m_socket->write(encodeFrame(
        MessageType::ReloadResult, QJsonDocument(result).toJson(QJsonDocument::Compact)));
}

QUrl ReloadController::resourceEntryUrl() const
{
    const auto uriPath = QString(m_moduleUri).replace(QLatin1Char('.'), QLatin1Char('/'));
    return QUrl(
        QStringLiteral("qrc:/qt/qml/") + uriPath + QLatin1Char('/') + m_entryType
        + QStringLiteral(".qml"));
}

} // namespace loom
