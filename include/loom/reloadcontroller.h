#pragma once

#include <QObject>
#include <QPointer>
#include <QUrl>

#include <memory>

class QQmlApplicationEngine;
class QTcpSocket;
class QTimer;
class QTemporaryDir;

namespace loom {

struct Bundle;

/// Loads the root QML scene and replaces it on request from a development
/// server. Use it directly only when not using loom::Application -- for
/// instance when embedding loom in an application that owns its own engine.
///
/// Bundles are written to a cache directory private to this controller, which
/// is removed on destruction; directories left by processes that are gone are
/// swept on the next start.
///
/// \sa docs/runtime-api.md, docs/protocol.md
class ReloadController final : public QObject {
    Q_OBJECT

public:
    explicit ReloadController(QQmlApplicationEngine &engine, QObject *parent = nullptr);
    ~ReloadController() override;

    /// Loads \a entryType from the compiled-in resources of \a moduleUri.
    /// \return false if the scene could not be created; see lastError().
    bool load(const QString &moduleUri, const QString &entryType);

    /// Connects to a development server and authenticates with \a token.
    /// Reconnects with bounded backoff if the connection drops or the server
    /// stops responding. \a host must be a loopback address.
    void
    connectToDevelopmentServer(const QString &host, quint16 port, const QString &token);

    /// Validates and applies a bundle, replacing the running scene.
    ///
    /// The incoming scene is compiled before the running one is touched, so a
    /// bundle that does not compile leaves the live scene untouched and costs
    /// nothing. A bundle whose id matches the running one is a no-op.
    /// \return false on failure, having rolled back to the last scene that
    ///         constructed successfully.
    bool applyBundle(const QByteArray &payload, QString *error = nullptr);

    /// The live root scene, or null if none could be constructed.
    QObject *rootObject() const;

    /// The most recent failure, as QML error text where applicable.
    QString lastError() const;

signals:
    /// A new scene is live. Also emitted when an already-active bundle is
    /// re-sent, in which case nothing was rebuilt.
    void sceneReloaded(const QString &bundleId);
    /// A bundle was rejected. The previous scene is still running.
    void reloadFailed(const QString &message);

private:
    // A dev server that is restarting comes back in seconds; one that is gone
    // should not be probed for the life of the process.
    static constexpr int InitialReconnectDelayMs = 250;
    static constexpr int MaximumReconnectDelayMs = 8000;
    static constexpr int MaximumReconnectAttempts = 10;
    // The development server pings on an interval. Silence well past it means
    // the link is half-open -- suspended host, killed server -- which TCP will
    // not report on its own. Generous relative to the server's interval so a
    // busy rebuild is never mistaken for a dead peer.
    static constexpr int ServerSilenceTimeoutMs = 30000;

    bool loadUrl(const QUrl &url, const QVariant &state, QString *error);
    bool ensureCacheDirectory(QString *error);
    bool
    materializeBundle(const Bundle &bundle, const QString &destination, QString *error);
    QVariant saveState() const;
    void restoreState(QObject *root, const QVariant &state);
    void openConnection();
    void scheduleReconnect(const QString &reason);
    void handleServerSilence();
    void handleSocketData();
    // Takes pre-rendered text rather than QList<QQmlError> so this installed
    // header does not drag QtQml into every consumer's translation unit.
    void reportEngineWarnings(const QString &text);
    void sendResult(bool success, const QString &message);
    QUrl resourceEntryUrl() const;
    QUrl bundleEntryUrl(const QString &directory) const;

    QQmlApplicationEngine &m_engine;
    // Owns this process's private bundle cache; removed when the controller is
    // destroyed. Held by pointer so the header need not include QTemporaryDir.
    std::unique_ptr<QTemporaryDir> m_cacheDirectory;
    QPointer<QObject> m_rootObject;
    QTcpSocket *m_socket = nullptr;
    QByteArray m_readBuffer;
    QString m_moduleUri;
    QString m_entryType;
    QString m_token;
    QString m_host;
    quint16 m_port = 0;
    int m_reconnectAttempts = 0;
    bool m_warningsConnected = false;
    QTimer *m_silenceWatchdog = nullptr;
    QString m_activeBundleId;
    QString m_activeBundleDirectory;
    QString m_previousBundleDirectory;
    QString m_lastError;
};

} // namespace loom
