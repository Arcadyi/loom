#include <loom/loom.h>
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
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

// QQmlData carries the line and column an object was declared at, which is how
// a live item is mapped back to the place in the source that produced it.
// loom_runtime already links Qt6::QmlPrivate for statecapture.cpp.
#include <private/qqmldata_p.h>

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
        qWarning("loom: could not remove %s from the bundle cache", qUtf8Printable(path));
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

// The paths whose contents differ from the scene that is running. Empty means
// "reload everything": either nothing is known about the running scene, or the
// file list itself changed, and a file that has appeared or disappeared cannot
// be swapped into a tree that was built without it.
QStringList ReloadController::changedFiles(const Bundle &bundle) const
{
    if (m_activeFileHashes.isEmpty() || bundle.files.size() != m_activeFileHashes.size())
        return {};
    QStringList changed;
    for (const auto &file : bundle.files) {
        const auto known = m_activeFileHashes.constFind(file.path);
        if (known == m_activeFileHashes.constEnd())
            return {};
        if (*known != file.sha256)
            changed.append(file.path);
    }
    return changed;
}

namespace {

// The document an object was built from, as a bundle-relative path. Bundles are
// staged into a fresh directory each time, so the tail is the part that can be
// compared with what the server sent.
bool objectCameFrom(QObject *object, const QString &bundleRelativePath)
{
    QQmlContext *context = qmlContext(object);
    if (!context)
        return false;
    const QString file = context->baseUrl().toLocalFile();
    return !file.isEmpty() && file.endsWith(QLatin1Char('/') + bundleRelativePath);
}

// Every object in the scene, each recorded against the Loader it sits behind --
// the smallest thing that can be rebuilt around it, because the Loader itself
// outlives the swap and whatever the surrounding document bound to it survives.
// Objects under no Loader map to nullptr: instantiated inline, they cannot be
// replaced without breaking the ids and bindings their parent holds, which is
// the whole reason a boundary has to be declared to get one.
//
// Descends visual children as well as QObject ones. A Loader's item is not
// reliably a QObject child of the Loader, and an item's children are parented
// visually, so either walk alone misses most of a scene.
void mapBoundaries(QObject *object, QObject *boundary, QHash<QObject *, QObject *> &into)
{
    if (!object || into.contains(object))
        return;
    into.insert(object, boundary);

    // Everything below a Loader is behind it, not just the item it currently
    // holds: the one it is replacing lingers as a child until the event loop
    // deletes it, and attributing that to the surrounding scene reads as a file
    // used outside any seam -- which is exactly the thing that forces a whole
    // reload, so a seam reload could not happen twice in a row.
    QObject *const inner = object->inherits("QQuickLoader") ? object : boundary;
    for (QObject *child : object->children())
        mapBoundaries(child, inner, into);
    if (auto *item = qobject_cast<QQuickItem *>(object)) {
        for (QQuickItem *child : item->childItems())
            mapBoundaries(child, inner, into);
    }
    if (inner == object)
        mapBoundaries(object->property("item").value<QObject *>(), object, into);
}

} // namespace

void ReloadController::reclaimStagedBundles()
{
    if (m_stagedDirectories.isEmpty())
        return;

    // A Loader hands the item it replaced to deleteLater, so until the event
    // loop gets to it the old scene is still hanging off the tree, still naming
    // the bundle it was read from. Settle those first or every intermediate
    // staging looks like something is reading it.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    QHash<QObject *, QObject *> live;
    mapBoundaries(m_rootObject, nullptr, live);
    QStringList reading;
    for (auto entry = live.cbegin(); entry != live.cend(); ++entry) {
        QQmlContext *context = qmlContext(entry.key());
        if (!context)
            continue;
        const QString file = context->baseUrl().toLocalFile();
        if (!file.isEmpty())
            reading.append(file);
    }

    QStringList retained;
    for (const QString &staged : std::as_const(m_stagedDirectories)) {
        // The active directory is what a failed reload rolls back to, so it
        // stays whether or not anything is reading it at this moment.
        bool needed = staged == m_activeBundleDirectory;
        const QString prefix = staged + QLatin1Char('/');
        for (const QString &file : std::as_const(reading)) {
            if (file.startsWith(prefix)) {
                needed = true;
                break;
            }
        }
        if (needed)
            retained.append(staged);
        else
            removePath(staged);
    }
    m_stagedDirectories = retained;
}

// Reloads the Loaders that contain the changed files and nothing else. Bails out
// -- leaving the scene untouched -- whenever the change cannot be confined,
// because a partial application of a bundle is worse than a whole one.
bool ReloadController::reloadBoundaries(
    const Bundle &bundle, const QString &destination, QString *error)
{
    const QStringList changed = changedFiles(bundle);
    if (changed.isEmpty() || !m_rootObject)
        return false;

    QHash<QObject *, QObject *> boundaryOf;
    mapBoundaries(m_rootObject, nullptr, boundaryOf);

    QSet<QObject *> boundaries;
    for (const QString &path : changed) {
        // A changed file with nothing built from it right now cannot be
        // reloaded, and leaving it would let a later navigation resolve the
        // stale copy out of the directory the scene is still rooted in.
        bool instantiated = false;
        for (auto entry = boundaryOf.cbegin(); entry != boundaryOf.cend(); ++entry) {
            if (!objectCameFrom(entry.key(), path))
                continue;
            instantiated = true;
            QObject *loader = entry.value();
            if (!loader)
                return false;
            // A Loader driven by sourceComponent has no URL to repoint.
            if (loader->property("source").toUrl().isEmpty())
                return false;
            boundaries.insert(loader);
        }
        if (!instantiated)
            return false;
    }
    if (boundaries.isEmpty())
        return false;

    for (QObject *loader : boundaries) {
        // source reads back as it was written, which is usually relative to the
        // document holding the Loader; only the resolved form names a file.
        const QVariant previous = loader->property("source");
        QQmlContext *context = qmlContext(loader);
        const QUrl resolved =
            context ? context->resolvedUrl(previous.toUrl()) : previous.toUrl();
        const QString file = resolved.toLocalFile();
        // Everything a bundle stages sits under qt/qml, so that is where the
        // staging directory ends and the part to carry across begins.
        const qsizetype cut = file.indexOf(QStringLiteral("/qt/qml/"));
        if (cut < 0)
            return false;
        const QUrl replacement = QUrl::fromLocalFile(destination + file.mid(cut));

        // The same envelope a whole reload uses, so a document behind a seam
        // keeps its loomSaveState() hook rather than only the properties the
        // generic capture can see. The hook exists for what capture cannot
        // reach, which is exactly what would go missing here.
        const QVariant state = saveState(loader->property("item").value<QObject *>());
        loader->setProperty("source", replacement);
        QObject *item = loader->property("item").value<QObject *>();
        if (!item) {
            loader->setProperty("source", previous);
            if (error) {
                *error =
                    QStringLiteral("could not reload %1").arg(replacement.toLocalFile());
            }
            return false;
        }
        restoreState(item, state);
    }
    return true;
}

bool ReloadController::applyBundle(const QByteArray &payload, QString *error)
{
    Bundle bundle;
    if (!decodeBundle(payload, bundle, error))
        return false;
    // Recorded before any early return: a bundle whose id already matches is
    // still the server telling us what it supports.
    m_serverCapabilities = bundle.capabilities;

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

    if (!m_stagedDirectories.contains(destination))
        m_stagedDirectories.append(destination);

    // Confined changes rebuild their own Loader and stop there: the window, the
    // scene around it and everything the engine has already compiled stay as
    // they are. The scene now spans two directories -- what was rebuilt reads
    // from this one, everything untouched still reads from the one before --
    // and reclaiming works that out from what the objects hold rather than
    // assuming the previous bundle is finished with.
    if (reloadBoundaries(bundle, destination, error)) {
        for (const auto &file : bundle.files)
            m_activeFileHashes.insert(file.path, file.sha256);
        m_activeBundleId = bundle.id;
        // Every staged bundle carries the whole project, so this one is a
        // complete scene on its own and the better thing to roll back to.
        m_previousBundleDirectory = m_activeBundleDirectory;
        m_activeBundleDirectory = destination;
        reclaimStagedBundles();
        emit sceneReloaded(bundle.id);
        return true;
    }

    const auto state = saveState(m_rootObject);
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

    m_previousBundleDirectory = m_activeBundleDirectory;
    m_activeBundleDirectory = destination;
    m_activeBundleId = bundle.id;
    m_activeFileHashes.clear();
    for (const auto &file : bundle.files)
        m_activeFileHashes.insert(file.path, file.sha256);
    // The scene was rebuilt whole, so anything staged that it does not read is
    // finished with -- including the directories a run of seam reloads left the
    // old parts of the tree pointing at.
    reclaimStagedBundles();
    emit sceneReloaded(bundle.id);
    return true;
}

QVariant ReloadController::saveState(QObject *root) const
{
    if (!root)
        return {};

    // Two halves in one envelope: every id-bearing object's declared
    // properties, captured with no cooperation from the scene, plus whatever
    // the root's loomSaveState() hook chose to return. The hook stays
    // because automatic capture deliberately skips bound properties and
    // objects with no id, and because a scene may want to carry something that
    // is not a property at all.
    QVariantMap envelope;

    const QVariantMap properties = captureSceneState(root);
    if (!properties.isEmpty())
        envelope.insert(QStringLiteral("properties"), properties);

    if (root->metaObject()->indexOfMethod("loomSaveState()") >= 0) {
        QVariant hookState;
        if (QMetaObject::invokeMethod(
                root, "loomSaveState", Qt::DirectConnection,
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
            sendResult(
                success, success ? QStringLiteral("Reloaded") : error,
                QStringLiteral("bundle"));
            if (!success)
                emit reloadFailed(error);
        } else if (frame.type == MessageType::Design) {
            QString error;
            const bool success = applyDesign(frame.payload, &error);
            m_lastError = error;
            sendResult(
                success, success ? QStringLiteral("Design reloaded") : error,
                QStringLiteral("design"));
            if (success)
                emit designReloaded();
            else
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

bool ReloadController::applyDesign(const QByteArray &payload, QString *error)
{
    Design design;
    if (!decodeDesign(payload, design, error))
        return false;

    // Parsed here rather than handed straight to the loader so a malformed file
    // is rejected before anything is touched. The scene is never involved:
    // tokens live in process-wide C++ that outlives every reload, so this
    // repaints the running window instead of rebuilding it.
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(design.tokens, &parseError);
    if (document.isNull() || !document.isObject()) {
        if (error) {
            *error =
                QStringLiteral("Design tokens are not a JSON object: %1 at offset %2")
                    .arg(parseError.errorString())
                    .arg(parseError.offset);
        }
        return false;
    }

    // Applied from memory against the project path the server reported. Staging
    // the bytes to a file and reloading from there resolved a relative
    // `iconRoot` against the staging directory, which pointed every icon at a
    // path nothing can open -- working in a compiled build, broken under dev.
    if (!loom::reloadConfigData(design.tokens, design.path)) {
        if (error)
            *error = QStringLiteral("Design tokens could not be applied");
        return false;
    }
    return true;
}

bool ReloadController::canEditSource() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState
        && m_serverCapabilities.contains(QLatin1String(CapabilityStyleEdit));
}

bool ReloadController::editStyle(
    QObject *item, const QString &oldStyle, const QString &newStyle)
{
    if (!item || !canEditSource())
        return false;

    // The declaration site, from QQmlData. This addresses the *source* rather
    // than the object, which is what makes it work for the items that have no
    // `id` -- most styled items -- and what makes the several instances of one
    // delegate all name the single place in the file that produced them.
    const QQmlData *const data = QQmlData::get(item);
    if (!data || data->lineNumber == 0)
        return false;

    QQmlContext *const context = qmlContext(item);
    if (!context)
        return false;
    const QString local = context->baseUrl().toLocalFile();
    // The tail after /qt/qml/ is byte-identical to the BundleFile::path the
    // server built, so it can resolve the file by lookup rather than by
    // reconstructing a path -- which is also why an edit cannot name a file the
    // server never bundled.
    const qsizetype marker = local.indexOf(QLatin1String("/qt/qml/"));
    if (marker < 0)
        return false;
    const QString bundlePath = local.mid(marker + 1);

    const StyleEdit edit{
        .path = bundlePath,
        .line = int(data->lineNumber),
        .column = int(data->columnNumber),
        .oldStyle = oldStyle,
        .newStyle = newStyle,
    };
    m_socket->write(encodeFrame(MessageType::StyleEdit, encodeStyleEdit(edit)));
    return true;
}

void ReloadController::sendResult(
    const bool success, const QString &message, const QString &kind)
{
    if (!m_socket)
        return;
    // `kind` distinguishes a scene reload from a design token reload. Without
    // it the server logged the active bundle id for both, so applying design
    // tokens -- which does not touch the bundle -- reported "reloaded bundle
    // <unchanged id>". Additive, so an older peer simply ignores it.
    const QJsonObject result{
        {QStringLiteral("success"), success},
        {QStringLiteral("bundleId"), m_activeBundleId},
        {QStringLiteral("kind"), kind},
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
