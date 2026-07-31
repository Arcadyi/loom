#pragma once

#include "projectmanifest.h"

#include <respin/protocol.h>

#include <QByteArray>
#include <QHash>
#include <QObject>

class QFileSystemWatcher;
class QTcpServer;
class QTcpSocket;
class QTimer;

class DevServer final : public QObject {
    Q_OBJECT

public:
    // `buildDirectory` is where qt_add_qml_module wrote the module's generated
    // qmldir. Bundling that file is what makes a development scene resolve types
    // the way the compiled build does -- singletons above all. Empty means "no
    // build tree", in which case no qmldir is bundled.
    explicit DevServer(
        const QString &projectRoot, ApplicationDefinition application,
        const QString &buildDirectory = {}, QObject *parent = nullptr);

    // Only as many application instances as a person plausibly runs against one
    // dev server. Past this, new connections are refused rather than queued, so
    // a connect loop cannot grow the client table without bound.
    static constexpr int MaximumClients = 8;
    // A client that has not proved itself within this long is dropped. Combined
    // with respin::MaximumPreAuthFrameSize this bounds what an unauthenticated
    // peer can hold: a few KiB, for a couple of seconds.
    static constexpr int AuthenticationTimeoutMs = 2000;
    // A half-open connection -- a suspended laptop, a hung application -- is
    // indistinguishable from an idle one at the TCP level. Without a heartbeat
    // the server keeps reporting "application connected" while every reload
    // goes nowhere. Ping is answered by the runtime; silence past the timeout
    // means the peer is gone.
    static constexpr int DefaultHeartbeatIntervalMs = 5000;
    static constexpr int DefaultHeartbeatTimeoutMs = 20000;

    // Tunable mainly so tests do not have to wait out a 20 second timeout; the
    // defaults are what a development session uses.
    void setHeartbeat(int intervalMs, int timeoutMs);

    bool start(QString *error = nullptr);
    quint16 port() const;
    QString token() const;
    int clientCount() const;

signals:
    void logMessage(const QString &message);
    void nativeFilesChanged();

private:
    // Replaces three QVariant properties stashed on the socket. Reading those
    // back copied the entire receive buffer twice per readyRead, which is
    // quadratic in bundle size on the one path that carries whole bundles.
    struct ClientState {
        QByteArray buffer;
        bool authenticated = false;
        // Monotonic; any frame counts as liveness, not just a Ping reply.
        qint64 lastSeenMs = 0;
    };

    void acceptConnection();
    void readClient(QTcpSocket *socket);
    void dropClient(QTcpSocket *socket, const QString &reason);
    bool authenticate(QTcpSocket *socket, ClientState &state, const respin::Frame &frame);
    void rebuildBundle();
    void resetWatchPaths();
    void resetNativeWatchPaths();
    void sendBundle(QTcpSocket *socket);
    void sendError(QTcpSocket *socket, const QString &message);
    void sendHeartbeats();
    // Reads the module's generated qmldir and strips the directives that only
    // make sense for the compiled build. Returns an empty array when there is
    // no build tree or no qmldir in it.
    QByteArray developmentQmldir() const;

    QString m_projectRoot;
    QString m_buildDirectory;
    ApplicationDefinition m_application;
    QTcpServer *m_server;
    QFileSystemWatcher *m_watcher;
    QFileSystemWatcher *m_nativeWatcher;
    QTimer *m_debounce;
    QTimer *m_nativeDebounce;
    QTimer *m_heartbeat;
    int m_heartbeatTimeoutMs = DefaultHeartbeatTimeoutMs;
    QHash<QTcpSocket *, ClientState> m_clients;
    QByteArray m_encodedBundle;
    QString m_bundleId;
    QString m_token;
};
