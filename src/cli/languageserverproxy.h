#pragma once

#include "lspdocument.h"
#include "lspjsonrpc.h"
#include "styleintelligence.h"
#include "styleworkspace.h"

#include <QFile>
#include <QHash>
#include <QJsonValue>
#include <QObject>
#include <QProcess>
#include <QStringList>

class QSocketNotifier;
#ifdef Q_OS_WIN
class QThread;
#endif

namespace lsp {

class LanguageServerProxy final : public QObject {
    Q_OBJECT

public:
    explicit LanguageServerProxy(QObject *parent = nullptr);
    int run(const QString &qmllsPath, const QStringList &qmllsArguments);
    static QString discoverQmlls(const QString &requested = {});

private:
    enum class PendingKind {
        Plain,
        Initialize,
        Completion,
        Colors,
        CodeActions,
        Shutdown
    };
    struct PendingRequest {
        PendingKind kind = PendingKind::Plain;
        QJsonValue loomResult;
    };

    void readEditor();
    void handleEditorBytes(const QByteArray &bytes);
    void readChild();
    void readChildError();
    void handleEditorMessage(const QJsonObject &message);
    void handleChildMessage(const QJsonObject &message);
    void sendEditor(const QJsonObject &message);
    void sendChild(const QJsonObject &message);
    void rememberRequest(
        const QJsonObject &message, PendingKind kind,
        const QJsonValue &loomResult = QJsonValue());
    void openDocument(const QJsonObject &params);
    void changeDocument(const QJsonObject &params);
    void closeDocument(const QJsonObject &params);
    void refreshDiagnostics(const QString &uri);
    void refreshAllDiagnostics();
    void publishDiagnostics(const QString &uri);
    void showWarning(const QString &message);
    void stop(int status);

    static QJsonValue mergeArrays(const QJsonValue &child, const QJsonValue &loom);
    static QJsonValue mergeCompletion(const QJsonValue &child, const QJsonValue &loom);
    static void augmentCapabilities(QJsonObject *response, PositionEncoding *encoding);
    static QString localPath(const QString &uri);
    qsizetype
    requestOffset(const QString &uri, const QJsonObject &params, bool *ok) const;

    QFile m_input;
    QFile m_output;
    QSocketNotifier *m_inputNotifier = nullptr;
#ifdef Q_OS_WIN
    // Held as the base type because the reader itself is a detail of the
    // implementation file.
    QThread *m_editorReader = nullptr;
#endif
    QProcess m_qmlls;
    JsonRpcFramer m_editorFramer;
    JsonRpcFramer m_childFramer;
    QHash<QString, PendingRequest> m_pending;
    QHash<QString, Document> m_documents;
    QHash<QString, QJsonArray> m_qmllsDiagnostics;
    QHash<QString, QJsonArray> m_loomDiagnostics;
    StyleWorkspace m_workspace;
    StyleIntelligence m_intelligence;
    PositionEncoding m_encoding = PositionEncoding::Utf16;
    bool m_initialized = false;
    bool m_stopping = false;
    bool m_shutdownRequested = false;
    int m_exitStatus = 0;
};

} // namespace lsp
