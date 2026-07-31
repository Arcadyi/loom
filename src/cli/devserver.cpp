#include "devserver.h"

#include <loom/protocol.h>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace {

// Compares without leaking, through timing, how many leading bytes matched.
// Ranked last among the transport hardening on purpose -- this is one guess per
// TCP connection over loopback against 256 bits -- but it costs three lines.
bool secureEquals(const QByteArray &left, const QByteArray &right)
{
    if (left.size() != right.size())
        return false;
    quint8 difference = 0;
    for (qsizetype index = 0; index < left.size(); ++index) {
        difference |=
            static_cast<quint8>(left.at(index)) ^ static_cast<quint8>(right.at(index));
    }
    return difference == 0;
}

QString randomToken()
{
    // system() is the OS CSPRNG. global() is a seeded Xoshiro256++ and is not
    // suitable for a credential that authorizes pushing executable QML into a
    // running process. Filling whole words also keeps all 256 bits, where
    // generate() & 0xff threw away three bytes in four.
    constexpr int words = 8;
    quint32 buffer[words];
    QRandomGenerator::system()->fillRange(buffer);
    const auto bytes = QByteArray(reinterpret_cast<const char *>(buffer), sizeof(buffer));
    return QString::fromLatin1(bytes.toHex());
}

QStringList filesUnder(const QString &root, const QStringList &extensions = {})
{
    QStringList files;
    QDirIterator iterator(
        root, QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const auto path = iterator.next();
        if (!extensions.isEmpty()
            && !extensions.contains(QFileInfo(path).suffix(), Qt::CaseInsensitive)) {
            continue;
        }
        files.append(path);
    }
    files.sort();
    return files;
}

} // namespace

DevServer::DevServer(
    const QString &projectRoot, ApplicationDefinition application,
    const QString &buildDirectory, QObject *parent)
    : QObject(parent)
    , m_projectRoot(projectRoot)
    , m_buildDirectory(buildDirectory)
    , m_application(std::move(application))
    , m_server(new QTcpServer(this))
    , m_watcher(new QFileSystemWatcher(this))
    , m_nativeWatcher(new QFileSystemWatcher(this))
    , m_debounce(new QTimer(this))
    , m_nativeDebounce(new QTimer(this))
    , m_heartbeat(new QTimer(this))
    , m_token(randomToken())
{
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(120);
    m_nativeDebounce->setSingleShot(true);
    m_nativeDebounce->setInterval(300);
    m_heartbeat->setInterval(DefaultHeartbeatIntervalMs);
    connect(m_heartbeat, &QTimer::timeout, this, &DevServer::sendHeartbeats);
    connect(m_debounce, &QTimer::timeout, this, &DevServer::rebuildBundle);
    connect(m_nativeDebounce, &QTimer::timeout, this, [this] {
        resetNativeWatchPaths();
        emit nativeFilesChanged();
    });
    connect(m_server, &QTcpServer::newConnection, this, &DevServer::acceptConnection);
    connect(
        m_watcher, &QFileSystemWatcher::fileChanged, m_debounce,
        qOverload<>(&QTimer::start));
    connect(
        m_watcher, &QFileSystemWatcher::directoryChanged, m_debounce,
        qOverload<>(&QTimer::start));
    connect(
        m_nativeWatcher, &QFileSystemWatcher::fileChanged, m_nativeDebounce,
        qOverload<>(&QTimer::start));
    connect(
        m_nativeWatcher, &QFileSystemWatcher::directoryChanged, m_nativeDebounce,
        qOverload<>(&QTimer::start));
    resetNativeWatchPaths();
}

void DevServer::setHeartbeat(const int intervalMs, const int timeoutMs)
{
    m_heartbeat->setInterval(intervalMs);
    m_heartbeatTimeoutMs = timeoutMs;
}

bool DevServer::start(QString *error)
{
    rebuildBundle();
    resetNativeWatchPaths();
    // Anything beyond this waits in the kernel backlog rather than becoming a
    // client table entry we then have to time out.
    m_server->setMaxPendingConnections(MaximumClients);
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        if (error)
            *error = m_server->errorString();
        return false;
    }
    m_heartbeat->start();
    emit logMessage(
        QStringLiteral("reload server listening on 127.0.0.1:%1").arg(port()));
    return true;
}

quint16 DevServer::port() const
{
    return m_server->serverPort();
}

QString DevServer::token() const
{
    return m_token;
}

int DevServer::clientCount() const
{
    return static_cast<int>(m_clients.size());
}

void DevServer::acceptConnection()
{
    while (m_server->hasPendingConnections()) {
        auto *socket = m_server->nextPendingConnection();
        if (m_clients.size() >= MaximumClients) {
            emit logMessage(QStringLiteral("refused a connection: already serving %1")
                                .arg(MaximumClients));
            socket->abort();
            socket->deleteLater();
            continue;
        }

        m_clients.insert(
            socket, ClientState{{}, false, QDateTime::currentMSecsSinceEpoch()});
        connect(
            socket, &QTcpSocket::readyRead, this, [this, socket] { readClient(socket); });
        // Prune here rather than sweeping later: the table is keyed by a raw
        // pointer, so an entry must not outlive its socket.
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_clients.remove(socket);
            socket->deleteLater();
        });
        connect(socket, &QTcpSocket::errorOccurred, this, [this, socket] {
            m_clients.remove(socket);
            socket->deleteLater();
        });

        // Bounded by the socket as context object, so it is cancelled if the
        // client disconnects first.
        QTimer::singleShot(AuthenticationTimeoutMs, socket, [this, socket] {
            const auto state = m_clients.constFind(socket);
            if (state != m_clients.cend() && !state->authenticated)
                dropClient(socket, QStringLiteral("did not authenticate in time"));
        });
    }
}

void DevServer::dropClient(QTcpSocket *socket, const QString &reason)
{
    emit logMessage(QStringLiteral("dropped a reload client: %1").arg(reason));
    m_clients.remove(socket);
    socket->disconnectFromHost();
}

bool DevServer::authenticate(
    QTcpSocket *socket, ClientState &state, const loom::Frame &frame)
{
    if (frame.type != loom::MessageType::Hello)
        return false;

    const auto hello = QJsonDocument::fromJson(frame.payload).object();
    if (hello.value(QStringLiteral("version")).toInt() != loom::ProtocolVersion) {
        // loom_runtime is a static library baked into the application, so an
        // upgraded CLI regularly meets an older runtime. Saying so beats
        // "unauthorized" followed by silence on both ends.
        sendError(
            socket,
            QStringLiteral(
                "protocol version mismatch: this loom speaks v%1, the application "
                "was built against v%2. Rebuild the application against the "
                "installed loom.")
                .arg(loom::ProtocolVersion)
                .arg(hello.value(QStringLiteral("version")).toInt()));
        return false;
    }
    if (!secureEquals(
            hello.value(QStringLiteral("token")).toString().toUtf8(), m_token.toUtf8())) {
        return false;
    }

    state.authenticated = true;
    emit logMessage(QStringLiteral("application connected"));

    // The application already has this scene; a multi-megabyte resend on every
    // restart buys nothing.
    if (!m_bundleId.isEmpty()
        && hello.value(QStringLiteral("currentBundle")).toString() == m_bundleId) {
        emit logMessage(
            QStringLiteral("application already has bundle %1").arg(m_bundleId));
        return true;
    }
    sendBundle(socket);
    return true;
}

void DevServer::readClient(QTcpSocket *socket)
{
    const auto entry = m_clients.find(socket);
    if (entry == m_clients.end())
        return;
    ClientState &state = *entry;

    state.buffer.append(socket->readAll());
    state.lastSeenMs = QDateTime::currentMSecsSinceEpoch();
    while (true) {
        loom::Frame frame;
        QString error;
        const auto limit = state.authenticated ? loom::MaximumFrameSize
                                               : loom::MaximumPreAuthFrameSize;
        if (!loom::takeFrame(state.buffer, frame, &error, limit)) {
            // A set error is a framing error, which is unrecoverable: takeFrame
            // consumed nothing and there is no way to resynchronize.
            if (!error.isEmpty())
                dropClient(socket, error);
            break;
        }
        if (!state.authenticated) {
            if (!authenticate(socket, state, frame)) {
                dropClient(socket, QStringLiteral("failed authentication"));
                break;
            }
        } else if (frame.type == loom::MessageType::ReloadResult) {
            const auto result = QJsonDocument::fromJson(frame.payload).object();
            emit logMessage(
                result.value(QStringLiteral("success")).toBool()
                    ? QStringLiteral("reloaded bundle %1")
                          .arg(result.value(QStringLiteral("bundleId")).toString())
                    : QStringLiteral("reload failed: %1")
                          .arg(result.value(QStringLiteral("message")).toString()));
        } else if (frame.type == loom::MessageType::Error) {
            const auto payload = QJsonDocument::fromJson(frame.payload).object();
            emit logMessage(
                QStringLiteral("application reported: %1")
                    .arg(payload.value(QStringLiteral("message")).toString()));
        }
    }
}

QByteArray DevServer::developmentQmldir() const
{
    if (m_buildDirectory.isEmpty())
        return {};
    const auto uriPath =
        QString(m_application.uri).replace(QLatin1Char('.'), QLatin1Char('/'));
    QFile generated(QDir(m_buildDirectory).filePath(uriPath + QStringLiteral("/qmldir")));
    if (!generated.open(QIODevice::ReadOnly))
        return {};

    QByteArrayList kept;
    for (const auto &line : generated.readAll().split('\n')) {
        const auto trimmed = line.trimmed();
        // "prefer :/qt/qml/<uri>/" tells the engine to load this module from the
        // compiled-in resources. Bundling it verbatim would make every reload a
        // no-op against the baked-in copy -- the absence of a qmldir is the only
        // reason development currently loads the bundled files at all. Dropping
        // it makes types resolve next to the qmldir, which is the bundle.
        if (trimmed.startsWith("prefer "))
            continue;
        // Names a .qmltypes file that is not bundled. Tooling metadata only, of
        // no use to the running engine.
        if (trimmed.startsWith("typeinfo "))
            continue;
        kept.append(line);
    }
    return kept.join('\n');
}

void DevServer::rebuildBundle()
{
    loom::Bundle bundle;
    QCryptographicHash aggregate(QCryptographicHash::Sha256);
    const auto uriPath =
        QString(m_application.uri).replace(QLatin1Char('.'), QLatin1Char('/'));

    auto appendRoot = [&](const QString &rootName, const QString &resourcePrefix,
                          const QStringList &extensions = QStringList{}) {
        const auto root = QDir(m_projectRoot).filePath(rootName);
        for (const auto &path : filesUnder(root, extensions)) {
            QFile input(path);
            if (!input.open(QIODevice::ReadOnly))
                continue;
            const auto contents = input.readAll();
            const auto relative = QDir(root).relativeFilePath(path);
            const auto resourcePath = QStringLiteral("qt/qml/") + uriPath
                + QLatin1Char('/') + resourcePrefix + relative;
            const auto hash =
                QCryptographicHash::hash(contents, QCryptographicHash::Sha256);
            bundle.files.append(loom::BundleFile{resourcePath, contents, hash});
            aggregate.addData(resourcePath.toUtf8());
            aggregate.addData(hash);
        }
    };
    for (const auto &root : m_application.qmlRoots)
        appendRoot(
            root, {},
            {QStringLiteral("qml"), QStringLiteral("js"), QStringLiteral("mjs")});
    for (const auto &root : m_application.assetRoots)
        appendRoot(root, QStringLiteral("assets/"));

    // The module declaration itself, so a development scene resolves types the
    // way the compiled build does. Without it a `pragma Singleton` type loads as
    // an ordinary component and the two builds disagree.
    if (const auto qmldir = developmentQmldir(); !qmldir.isEmpty()) {
        const auto resourcePath =
            QStringLiteral("qt/qml/") + uriPath + QStringLiteral("/qmldir");
        const auto hash = QCryptographicHash::hash(qmldir, QCryptographicHash::Sha256);
        bundle.files.append(loom::BundleFile{resourcePath, qmldir, hash});
        aggregate.addData(resourcePath.toUtf8());
        aggregate.addData(hash);
    }

    // 32 hex characters, not 16. The cache reuses a directory whenever the id
    // matches, so a collision loads the wrong scene; 64 bits was a needless
    // cliff when the field costs nothing.
    bundle.id = QString::fromLatin1(aggregate.result().toHex().left(32));
    if (bundle.id == m_bundleId) {
        resetWatchPaths();
        return;
    }

    // Checked here, before anything is committed. encodeBundle never returns
    // empty, so the old check never fired; the limit actually bit inside
    // encodeFrame, which returns {} and makes write() a silent no-op while the
    // application waits forever. Committing m_bundleId first also meant the
    // same oversized bundle was never retried.
    const auto encoded = loom::encodeBundle(bundle);
    if (encoded.size() + 1 > loom::MaximumFrameSize) {
        const auto message = QStringLiteral(
                                 "bundle is %1 MiB, over the %2 MiB "
                                 "development protocol limit; not sent")
                                 .arg(encoded.size() / (1024 * 1024))
                                 .arg(loom::MaximumFrameSize / (1024 * 1024));
        emit logMessage(message);
        for (auto client = m_clients.cbegin(); client != m_clients.cend(); ++client) {
            if (client.value().authenticated)
                sendError(client.key(), message);
        }
        resetWatchPaths();
        return;
    }

    m_bundleId = bundle.id;
    m_encodedBundle = encoded;
    resetWatchPaths();
    emit logMessage(QStringLiteral("prepared bundle %1 (%2 files)")
                        .arg(bundle.id)
                        .arg(bundle.files.size()));
    for (auto client = m_clients.cbegin(); client != m_clients.cend(); ++client) {
        if (client.value().authenticated)
            sendBundle(client.key());
    }
}

void DevServer::resetWatchPaths()
{
    const auto watched = m_watcher->files() + m_watcher->directories();
    if (!watched.isEmpty())
        m_watcher->removePaths(watched);

    QStringList paths;
    const auto roots = m_application.qmlRoots + m_application.assetRoots;
    for (const auto &rootName : roots) {
        const auto root = QDir(m_projectRoot).filePath(rootName);
        if (!QFileInfo::exists(root))
            continue;
        paths.append(root);
        QDirIterator iterator(
            root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (iterator.hasNext())
            paths.append(iterator.next());
        paths.append(filesUnder(root));
    }
    if (!paths.isEmpty())
        m_watcher->addPaths(paths);
}

void DevServer::resetNativeWatchPaths()
{
    const auto watched = m_nativeWatcher->files() + m_nativeWatcher->directories();
    if (!watched.isEmpty())
        m_nativeWatcher->removePaths(watched);

    QStringList paths;
    for (const auto &relative : {
             QStringLiteral("CMakeLists.txt"),
             QStringLiteral("loom.json"),
             QStringLiteral("src"),
             QStringLiteral("cmake"),
         }) {
        const auto path = QDir(m_projectRoot).filePath(relative);
        const QFileInfo info(path);
        if (!info.exists())
            continue;
        paths.append(path);
        if (!info.isDir())
            continue;
        QDirIterator directories(
            path, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (directories.hasNext())
            paths.append(directories.next());
        paths.append(filesUnder(path));
    }
    if (!paths.isEmpty())
        m_nativeWatcher->addPaths(paths);
}

void DevServer::sendBundle(QTcpSocket *socket)
{
    if (m_encodedBundle.isEmpty())
        return;
    socket->write(loom::encodeFrame(loom::MessageType::Bundle, m_encodedBundle));
}

void DevServer::sendHeartbeats()
{
    const auto now = QDateTime::currentMSecsSinceEpoch();
    // Collected first: dropClient mutates m_clients, which cannot happen while
    // iterating it.
    QList<QTcpSocket *> silent;
    QList<QTcpSocket *> alive;
    for (auto client = m_clients.cbegin(); client != m_clients.cend(); ++client) {
        if (!client.value().authenticated)
            continue;
        if (now - client.value().lastSeenMs > m_heartbeatTimeoutMs)
            silent.append(client.key());
        else
            alive.append(client.key());
    }

    for (auto *socket : silent) {
        dropClient(
            socket, QStringLiteral("no response for %1 ms").arg(m_heartbeatTimeoutMs));
    }
    for (auto *socket : alive)
        socket->write(loom::encodeFrame(loom::MessageType::Ping, {}));
}

void DevServer::sendError(QTcpSocket *socket, const QString &message)
{
    const QJsonObject payload{{QStringLiteral("message"), message}};
    socket->write(
        loom::encodeFrame(
            loom::MessageType::Error,
            QJsonDocument(payload).toJson(QJsonDocument::Compact)));
    // The caller usually disconnects immediately afterwards, and an unflushed
    // write dies with the socket.
    socket->flush();
}
