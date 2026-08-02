#include "languageserverproxy.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibraryInfo>
#include <QMutex>
#include <QProcessEnvironment>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#ifdef Q_OS_WIN
#include <QThread>
#include <cstdio>
#include <functional>
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace lsp {

namespace {

#ifdef Q_OS_WIN
// Nothing on Windows lets an event loop watch the pipe an editor hands a
// language server for readability: QWinEventNotifier reports handles the
// Windows wait functions can signal, and a pipe is not one of them. Read on a
// thread that is allowed to block instead, and hand each chunk to the proxy's
// own thread.
class StdinReader final : public QThread {
public:
    StdinReader(
        QObject *receiver, std::function<void(const QByteArray &)> onBytes,
        std::function<void()> onClosed)
        : QThread(receiver)
        , m_receiver(receiver)
        , m_onBytes(std::move(onBytes))
        , m_onClosed(std::move(onClosed))
    {
    }

protected:
    void run() override
    {
        // _read rather than ReadFile on the standard handle: it hands back
        // whatever has arrived instead of waiting for the buffer to fill, and
        // it reads the descriptor the parent set up, which is the one an
        // ordinary console application would read and the one QProcess feeds.
        trace("reader started");
        QByteArray buffer(64 * 1024, Qt::Uninitialized);
        for (;;) {
            const int read = _read(
                _fileno(stdin), buffer.data(), static_cast<unsigned int>(buffer.size()));
            if (read <= 0) {
                trace("reader ended", QString::number(read));
                QMetaObject::invokeMethod(m_receiver, m_onClosed, Qt::QueuedConnection);
                return;
            }
            trace("reader read", QString::number(read));
            QMetaObject::invokeMethod(
                m_receiver,
                [callback = m_onBytes,
                 chunk = QByteArray(buffer.constData(), static_cast<qsizetype>(read))] {
                    callback(chunk);
                },
                Qt::QueuedConnection);
        }
    }

private:
    QObject *m_receiver;
    std::function<void(const QByteArray &)> m_onBytes;
    std::function<void()> m_onClosed;
};
#endif

QJsonObject responseFor(const QJsonObject &request, const QJsonValue &result)
{
    return {
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), request.value(QStringLiteral("id"))},
        {QStringLiteral("result"), result},
    };
}

QString executableAt(const QString &directory)
{
    if (directory.isEmpty())
        return {};
    auto name = QStringLiteral("qmlls");
#ifdef Q_OS_WIN
    name += QStringLiteral(".exe");
#endif
    const QString path = QDir(directory).filePath(name);
    return QFileInfo(path).isExecutable() ? path : QString();
}

QString resolvedExecutable(const QString &requested)
{
    const QFileInfo explicitPath(requested);
    if (explicitPath.isExecutable())
        return explicitPath.absoluteFilePath();
    return QStandardPaths::findExecutable(requested);
}

QString canonicalExecutablePath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

bool isCurrentExecutable(const QString &path)
{
    if (path.isEmpty())
        return false;
    const auto current = canonicalExecutablePath(QCoreApplication::applicationFilePath());
    const auto candidate = canonicalExecutablePath(path);
#ifdef Q_OS_WIN
    return current.compare(candidate, Qt::CaseInsensitive) == 0;
#else
    return current == candidate;
#endif
}

QString externalExecutable(const QString &path)
{
    return isCurrentExecutable(path) ? QString() : path;
}

QString executableReportedBy(const QStringList &toolNames, const QStringList &arguments)
{
    for (const auto &toolName : toolNames) {
        const QString tool = QStandardPaths::findExecutable(toolName);
        if (tool.isEmpty())
            continue;
        QProcess query;
        query.start(tool, arguments);
        if (!query.waitForFinished(3000) || query.exitCode() != 0)
            continue;
        const QString found =
            executableAt(QString::fromLocal8Bit(query.readAllStandardOutput()).trimmed());
        if (!externalExecutable(found).isEmpty())
            return found;
    }
    return {};
}

QJsonArray completionItems(const QJsonValue &value)
{
    if (value.isArray())
        return value.toArray();
    return value.toObject().value(QStringLiteral("items")).toArray();
}

} // namespace

LanguageServerProxy::LanguageServerProxy(QObject *parent)
    : QObject(parent)
    , m_workspace(this)
    , m_intelligence(&m_workspace)
{
    connect(
        &m_qmlls, &QProcess::readyReadStandardOutput, this,
        &LanguageServerProxy::readChild);
    connect(
        &m_qmlls, &QProcess::readyReadStandardError, this,
        &LanguageServerProxy::readChildError);
    connect(
        &m_qmlls, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (m_stopping)
                return;
            showWarning(QStringLiteral("qmlls failed: %1").arg(m_qmlls.errorString()));
            if (error == QProcess::FailedToStart || error == QProcess::Crashed)
                stop(error == QProcess::FailedToStart ? 127 : 128);
        });
    connect(
        &m_qmlls, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int exitCode, QProcess::ExitStatus status) {
            if (m_stopping || m_shutdownRequested)
                return;
            showWarning(QStringLiteral("qmlls exited unexpectedly (%1)").arg(exitCode));
            stop(status == QProcess::CrashExit ? 128 : (exitCode == 0 ? 1 : exitCode));
        });
    connect(
        &m_workspace, &StyleWorkspace::vocabularyChanged, this,
        &LanguageServerProxy::refreshAllDiagnostics);
    connect(
        &m_workspace, &StyleWorkspace::warning, this, &LanguageServerProxy::showWarning);
}

QString LanguageServerProxy::discoverQmlls(const QString &requested)
{
    if (!requested.isEmpty())
        return externalExecutable(resolvedExecutable(requested));

    const QString fromEnvironment = qEnvironmentVariable("LOOM_QMLLS_PATH");
    if (!fromEnvironment.isEmpty()) {
        const QString resolved = discoverQmlls(fromEnvironment);
        if (!resolved.isEmpty())
            return resolved;
    }

    if (const QString bundled =
            executableAt(QLibraryInfo::path(QLibraryInfo::BinariesPath));
        !externalExecutable(bundled).isEmpty()) {
        return bundled;
    }

    if (const QString fromQtPaths = executableReportedBy(
            {QStringLiteral("qtpaths6"), QStringLiteral("qtpaths")},
            {QStringLiteral("--query"), QStringLiteral("QT_INSTALL_BINS")});
        !fromQtPaths.isEmpty()) {
        return fromQtPaths;
    }
    // qmake6 is commonly exposed in /usr/bin even when qtpaths6 is not.
    if (const QString fromQmake = executableReportedBy(
            {QStringLiteral("qmake6"), QStringLiteral("qmake")},
            {QStringLiteral("-query"), QStringLiteral("QT_INSTALL_BINS")});
        !fromQmake.isEmpty()) {
        return fromQmake;
    }
    // Last resort before plain PATH: distributions can expose only Qt 5's
    // qtpaths while keeping Qt 6 host tools in a private bindir. The path came
    // from the Qt host package used to build Loom and is ignored once it no
    // longer names an executable.
    if (const QString configured =
            externalExecutable(QStringLiteral(LOOM_QMLLS_FALLBACK_PATH));
        QFileInfo(configured).isExecutable()) {
        return configured;
    }
    return externalExecutable(QStandardPaths::findExecutable(QStringLiteral("qmlls")));
}

int LanguageServerProxy::run(const QString &qmllsPath, const QStringList &qmllsArguments)
{
    trace("run", qmllsPath);
    const QString executable = discoverQmlls(qmllsPath);
    trace("qmlls resolved", executable);
    if (executable.isEmpty()) {
        QTextStream(stderr)
            << "loom lsp: qmlls was not found; pass --qmlls <path> or set "
               "LOOM_QMLLS_PATH\n";
        return 127;
    }

    m_qmlls.setProgram(executable);
    m_qmlls.setArguments(qmllsArguments);
    m_qmlls.setProcessChannelMode(QProcess::SeparateChannels);
    m_qmlls.start();
    if (!m_qmlls.waitForStarted(5000)) {
        trace("qmlls did not start", m_qmlls.errorString());
        QTextStream(stderr) << "loom lsp: could not start " << executable << ": "
                            << m_qmlls.errorString() << '\n';
        return 127;
    }
    trace("qmlls started");

    useBinaryStdio();
    if (!m_input.open(stdin, QIODevice::ReadOnly, QFileDevice::DontCloseHandle)
        || !m_output.open(stdout, QIODevice::WriteOnly, QFileDevice::DontCloseHandle)) {
        QTextStream(stderr) << "loom lsp: could not open the stdio transport\n";
        m_qmlls.kill();
        m_qmlls.waitForFinished();
        return 1;
    }

#ifdef Q_OS_WIN
    m_editorReader = new StdinReader(
        this, [this](const QByteArray &bytes) { handleEditorBytes(bytes); },
        [this] { stop(0); });
    m_editorReader->start();
#else
    m_inputNotifier = new QSocketNotifier(fileno(stdin), QSocketNotifier::Read, this);
    connect(
        m_inputNotifier, &QSocketNotifier::activated, this, [this]() { readEditor(); });
#endif
    trace("waiting for the editor");

    QCoreApplication::exec();

#ifdef Q_OS_WIN
    // The reader is parked inside ReadFile and only that call returning ends
    // it, so cancel the read rather than wait on one that will not come back.
    if (m_editorReader) {
        CancelIoEx(GetStdHandle(STD_INPUT_HANDLE), nullptr);
        if (!m_editorReader->wait(2000)) {
            m_editorReader->terminate();
            m_editorReader->wait(1000);
        }
    }
#endif
    return m_exitStatus;
}

void LanguageServerProxy::readEditor()
{
    const QByteArray bytes = m_input.read(64 * 1024);
    if (bytes.isEmpty()) {
        if (m_input.atEnd())
            stop(0);
        return;
    }
    handleEditorBytes(bytes);
}

void LanguageServerProxy::handleEditorBytes(const QByteArray &bytes)
{
    if (m_stopping)
        return;
    QString error;
    const auto messages = m_editorFramer.push(bytes, &error);
    trace(
        "editor bytes",
        QStringLiteral("%1 -> %2 message(s) %3")
            .arg(bytes.size())
            .arg(messages.size())
            .arg(error));
    if (!error.isEmpty())
        QTextStream(stderr) << "loom lsp: " << error << '\n';
    for (const auto &message : messages)
        handleEditorMessage(message);
}

void LanguageServerProxy::readChild()
{
    QString error;
    const auto messages = m_childFramer.push(m_qmlls.readAllStandardOutput(), &error);
    trace("from qmlls", QStringLiteral("%1 message(s)").arg(messages.size()));
    if (!error.isEmpty())
        QTextStream(stderr) << "loom lsp: invalid qmlls output: " << error << '\n';
    for (const auto &message : messages)
        handleChildMessage(message);
}

void LanguageServerProxy::readChildError()
{
    const QByteArray bytes = m_qmlls.readAllStandardError();
    if (!bytes.isEmpty()) {
        QFile errorOutput;
        if (errorOutput.open(
                stderr, QIODevice::WriteOnly, QFileDevice::DontCloseHandle)) {
            errorOutput.write(bytes);
            errorOutput.flush();
        }
    }
}

void LanguageServerProxy::sendEditor(const QJsonObject &message)
{
    const QByteArray framed = JsonRpcFramer::frame(message);
    trace("to editor", QString::number(framed.size()));
    qsizetype written = 0;
    while (written < framed.size()) {
        const qint64 chunk =
            m_output.write(framed.constData() + written, framed.size() - written);
        if (chunk <= 0) {
            stop(1);
            return;
        }
        written += chunk;
    }
    if (!m_output.flush())
        stop(1);
}

void LanguageServerProxy::sendChild(const QJsonObject &message)
{
    if (m_qmlls.state() == QProcess::Running) {
        const QByteArray framed = JsonRpcFramer::frame(message);
        const qint64 written = m_qmlls.write(framed);
        trace("to qmlls", QStringLiteral("%1 of %2").arg(written).arg(framed.size()));
        if (written != framed.size())
            stop(1);
    }
}

void LanguageServerProxy::rememberRequest(
    const QJsonObject &message, PendingKind kind, const QJsonValue &loomResult)
{
    m_pending.insert(
        requestIdKey(message.value(QStringLiteral("id"))), {kind, loomResult});
}

QString LanguageServerProxy::localPath(const QString &uri)
{
    return QUrl(uri).isLocalFile() ? QUrl(uri).toLocalFile() : uri;
}

qsizetype LanguageServerProxy::requestOffset(
    const QString &uri, const QJsonObject &params, bool *ok) const
{
    if (ok)
        *ok = false;
    const auto document = m_documents.constFind(uri);
    if (document == m_documents.constEnd())
        return 0;
    const QJsonObject position = params.value(QStringLiteral("position")).toObject();
    return document->offsetAt(
        position.value(QStringLiteral("line")).toInt(-1),
        position.value(QStringLiteral("character")).toInt(-1), m_encoding, ok);
}

void LanguageServerProxy::handleEditorMessage(const QJsonObject &message)
{
    const QString method = message.value(QStringLiteral("method")).toString();
    const QJsonObject params = message.value(QStringLiteral("params")).toObject();
    const bool request = message.contains(QStringLiteral("id")) && !method.isEmpty();

    if (method == QStringLiteral("textDocument/didOpen")) {
        openDocument(params);
        sendChild(message);
        return;
    }
    if (method == QStringLiteral("textDocument/didChange")) {
        changeDocument(params);
        sendChild(message);
        return;
    }
    if (method == QStringLiteral("textDocument/didClose")) {
        closeDocument(params);
        sendChild(message);
        return;
    }
    if (method == QStringLiteral("textDocument/completion") && request) {
        const QString uri = params.value(QStringLiteral("textDocument"))
                                .toObject()
                                .value(QStringLiteral("uri"))
                                .toString();
        bool offsetOk = false;
        const qsizetype offset = requestOffset(uri, params, &offsetOk);
        bool inStyle = false;
        QJsonValue loomResult;
        if (offsetOk) {
            loomResult = m_intelligence.completion(
                localPath(uri), m_documents.value(uri), offset, m_encoding, &inStyle);
        }
        rememberRequest(
            message, PendingKind::Completion,
            inStyle ? loomResult : QJsonValue(QJsonValue::Undefined));
        sendChild(message);
        return;
    }
    if (method == QStringLiteral("textDocument/hover") && request) {
        const QString uri = params.value(QStringLiteral("textDocument"))
                                .toObject()
                                .value(QStringLiteral("uri"))
                                .toString();
        bool offsetOk = false;
        const qsizetype offset = requestOffset(uri, params, &offsetOk);
        if (offsetOk) {
            StyleToken token;
            if (m_documents.value(uri).styleTokenAt(offset, &token)) {
                sendEditor(responseFor(
                    message,
                    m_intelligence.hover(
                        localPath(uri), m_documents.value(uri), offset, m_encoding)));
                return;
            }
        }
        rememberRequest(message, PendingKind::Plain);
        sendChild(message);
        return;
    }
    if (method == QStringLiteral("textDocument/documentColor") && request) {
        const QString uri = params.value(QStringLiteral("textDocument"))
                                .toObject()
                                .value(QStringLiteral("uri"))
                                .toString();
        const QJsonArray loomColors = m_documents.contains(uri)
            ? m_intelligence.colors(localPath(uri), m_documents.value(uri), m_encoding)
            : QJsonArray{};
        rememberRequest(message, PendingKind::Colors, loomColors);
        sendChild(message);
        return;
    }
    if (method == QStringLiteral("textDocument/colorPresentation") && request) {
        // Loom utilities do not accept arbitrary color literals, so the color
        // picker has no valid class edit to offer.
        const QString uri = params.value(QStringLiteral("textDocument"))
                                .toObject()
                                .value(QStringLiteral("uri"))
                                .toString();
        const QJsonObject start = params.value(QStringLiteral("range"))
                                      .toObject()
                                      .value(QStringLiteral("start"))
                                      .toObject();
        const auto document = m_documents.constFind(uri);
        if (document != m_documents.constEnd()) {
            bool offsetOk = false;
            const qsizetype offset = document->offsetAt(
                start.value(QStringLiteral("line")).toInt(-1),
                start.value(QStringLiteral("character")).toInt(-1), m_encoding,
                &offsetOk);
            StyleToken token;
            if (offsetOk && document->styleTokenAt(offset, &token)) {
                sendEditor(responseFor(message, QJsonArray{}));
                return;
            }
        }
        rememberRequest(message, PendingKind::Plain);
        sendChild(message);
        return;
    }
    if (method == QStringLiteral("textDocument/codeAction") && request) {
        const QString uri = params.value(QStringLiteral("textDocument"))
                                .toObject()
                                .value(QStringLiteral("uri"))
                                .toString();
        const QJsonArray actions = m_intelligence.codeActions(
            uri,
            params.value(QStringLiteral("context"))
                .toObject()
                .value(QStringLiteral("diagnostics"))
                .toArray());
        rememberRequest(message, PendingKind::CodeActions, actions);
        sendChild(message);
        return;
    }
    if (method == QStringLiteral("initialize") && request) {
        rememberRequest(message, PendingKind::Initialize);
        sendChild(message);
        return;
    }
    if (method == QStringLiteral("initialized"))
        m_initialized = true;
    if (method == QStringLiteral("shutdown") && request) {
        m_shutdownRequested = true;
        rememberRequest(message, PendingKind::Shutdown);
        sendChild(message);
        return;
    }
    if (method == QStringLiteral("exit")) {
        sendChild(message);
        stop(m_shutdownRequested ? 0 : 1);
        return;
    }

    if (request)
        rememberRequest(message, PendingKind::Plain);
    sendChild(message);
}

void LanguageServerProxy::handleChildMessage(const QJsonObject &message)
{
    const QString method = message.value(QStringLiteral("method")).toString();
    if (!method.isEmpty()) {
        if (method == QStringLiteral("textDocument/publishDiagnostics")) {
            const QJsonObject params = message.value(QStringLiteral("params")).toObject();
            const QString uri = params.value(QStringLiteral("uri")).toString();
            QJsonArray diagnostics;
            for (const auto &entry :
                 params.value(QStringLiteral("diagnostics")).toArray()) {
                if (entry.toObject().value(QStringLiteral("source")).toString()
                    != QStringLiteral("loom")) {
                    diagnostics.append(entry);
                }
            }
            m_qmllsDiagnostics.insert(uri, diagnostics);
            publishDiagnostics(uri);
            return;
        }
        sendEditor(message);
        return;
    }

    if (!message.contains(QStringLiteral("id"))) {
        sendEditor(message);
        return;
    }
    const QString key = requestIdKey(message.value(QStringLiteral("id")));
    const auto pending = m_pending.take(key);
    if (pending.kind == PendingKind::Plain || pending.kind == PendingKind::Shutdown) {
        sendEditor(message);
        return;
    }

    QJsonObject response = message;
    if (pending.kind == PendingKind::Initialize) {
        augmentCapabilities(&response, &m_encoding);
        sendEditor(response);
        return;
    }

    const QJsonValue childResult = response.value(QStringLiteral("result"));
    QJsonValue merged;
    if (pending.kind == PendingKind::Completion)
        merged = mergeCompletion(childResult, pending.loomResult);
    else
        merged = mergeArrays(childResult, pending.loomResult);
    if (!pending.loomResult.isUndefined())
        response.remove(QStringLiteral("error"));
    response.insert(QStringLiteral("result"), merged);
    sendEditor(response);
}

void LanguageServerProxy::openDocument(const QJsonObject &params)
{
    const QJsonObject item = params.value(QStringLiteral("textDocument")).toObject();
    const QString uri = item.value(QStringLiteral("uri")).toString();
    Document document;
    document.open(
        item.value(QStringLiteral("text")).toString(),
        item.value(QStringLiteral("version")).toInt());
    m_documents.insert(uri, document);
    refreshDiagnostics(uri);
}

void LanguageServerProxy::changeDocument(const QJsonObject &params)
{
    const QJsonObject item = params.value(QStringLiteral("textDocument")).toObject();
    const QString uri = item.value(QStringLiteral("uri")).toString();
    auto document = m_documents.find(uri);
    if (document == m_documents.end())
        return;
    QString error;
    if (!document->applyChanges(
            params.value(QStringLiteral("contentChanges")).toArray(),
            item.value(QStringLiteral("version")).toInt(document->version()), m_encoding,
            &error)) {
        showWarning(error);
        return;
    }
    refreshDiagnostics(uri);
}

void LanguageServerProxy::closeDocument(const QJsonObject &params)
{
    const QString uri = params.value(QStringLiteral("textDocument"))
                            .toObject()
                            .value(QStringLiteral("uri"))
                            .toString();
    m_documents.remove(uri);
    m_qmllsDiagnostics.remove(uri);
    m_loomDiagnostics.remove(uri);
    sendEditor(
        QJsonObject{
            {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("method"), QStringLiteral("textDocument/publishDiagnostics")},
            {QStringLiteral("params"),
             QJsonObject{
                 {QStringLiteral("uri"), uri},
                 {QStringLiteral("diagnostics"), QJsonArray{}},
             }},
        });
}

void LanguageServerProxy::refreshDiagnostics(const QString &uri)
{
    const auto document = m_documents.constFind(uri);
    if (document == m_documents.constEnd())
        return;
    m_loomDiagnostics.insert(
        uri, m_intelligence.diagnostics(localPath(uri), *document, m_encoding));
    publishDiagnostics(uri);
}

void LanguageServerProxy::refreshAllDiagnostics()
{
    const QStringList uris = m_documents.keys();
    for (const auto &uri : uris)
        refreshDiagnostics(uri);
}

void LanguageServerProxy::publishDiagnostics(const QString &uri)
{
    QJsonArray diagnostics = m_qmllsDiagnostics.value(uri);
    for (const auto &diagnostic : m_loomDiagnostics.value(uri))
        diagnostics.append(diagnostic);
    QJsonObject params{
        {QStringLiteral("uri"), uri},
        {QStringLiteral("diagnostics"), diagnostics},
    };
    if (m_documents.contains(uri))
        params.insert(QStringLiteral("version"), m_documents.value(uri).version());
    sendEditor(
        QJsonObject{
            {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("method"), QStringLiteral("textDocument/publishDiagnostics")},
            {QStringLiteral("params"), params},
        });
}

void LanguageServerProxy::showWarning(const QString &message)
{
    if (!m_initialized) {
        QTextStream(stderr) << "loom lsp: " << message << '\n';
        return;
    }
    sendEditor(
        QJsonObject{
            {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("method"), QStringLiteral("window/showMessage")},
            {QStringLiteral("params"),
             QJsonObject{
                 {QStringLiteral("type"), 2},
                 {QStringLiteral("message"), QStringLiteral("Loom: ") + message},
             }},
        });
}

void LanguageServerProxy::stop(int status)
{
    if (m_stopping)
        return;
    m_stopping = true;
    m_exitStatus = status;
    if (m_inputNotifier)
        m_inputNotifier->setEnabled(false);
    // The Windows reader has no switch to turn off from here -- it is parked in
    // a blocking read that only the cancellation after exec() returns ends. Any
    // chunk it already queued is dropped by handleEditorBytes instead.
    if (m_qmlls.state() != QProcess::NotRunning) {
        m_qmlls.closeWriteChannel();
        if (!m_qmlls.waitForFinished(1000)) {
            m_qmlls.terminate();
            if (!m_qmlls.waitForFinished(1000)) {
                m_qmlls.kill();
                m_qmlls.waitForFinished();
            }
        }
    }
    QCoreApplication::exit(status);
}

QJsonValue
LanguageServerProxy::mergeArrays(const QJsonValue &child, const QJsonValue &loom)
{
    if (loom.isUndefined())
        return child;
    QJsonArray merged = child.toArray();
    for (const auto &item : loom.toArray())
        merged.append(item);
    return merged;
}

QJsonValue
LanguageServerProxy::mergeCompletion(const QJsonValue &child, const QJsonValue &loom)
{
    if (loom.isUndefined())
        return child;
    QJsonArray items = completionItems(child);
    for (const auto &item : completionItems(loom))
        items.append(item);
    QJsonObject result = child.isObject() ? child.toObject() : QJsonObject{};
    result.insert(QStringLiteral("items"), items);
    const bool incomplete = result.value(QStringLiteral("isIncomplete")).toBool()
        || loom.toObject().value(QStringLiteral("isIncomplete")).toBool();
    result.insert(QStringLiteral("isIncomplete"), incomplete);
    return result;
}

void LanguageServerProxy::augmentCapabilities(
    QJsonObject *response, PositionEncoding *encoding)
{
    QJsonObject result = response->value(QStringLiteral("result")).toObject();
    QJsonObject capabilities = result.value(QStringLiteral("capabilities")).toObject();
    const QString positionEncoding =
        capabilities.value(QStringLiteral("positionEncoding")).toString();
    if (positionEncoding == QStringLiteral("utf-8"))
        *encoding = PositionEncoding::Utf8;
    else if (positionEncoding == QStringLiteral("utf-32"))
        *encoding = PositionEncoding::Utf32;
    else
        *encoding = PositionEncoding::Utf16;

    QJsonObject completion =
        capabilities.value(QStringLiteral("completionProvider")).toObject();
    QSet<QString> triggers;
    for (const auto &value :
         completion.value(QStringLiteral("triggerCharacters")).toArray())
        triggers.insert(value.toString());
    for (const auto &trigger :
         {QStringLiteral(" "), QStringLiteral("\""), QStringLiteral(":"),
          QStringLiteral("-"), QStringLiteral("/")}) {
        triggers.insert(trigger);
    }
    QStringList sortedTriggers = triggers.values();
    sortedTriggers.sort();
    completion.insert(
        QStringLiteral("triggerCharacters"), QJsonArray::fromStringList(sortedTriggers));
    capabilities.insert(QStringLiteral("completionProvider"), completion);
    if (!capabilities.contains(QStringLiteral("textDocumentSync")))
        capabilities.insert(QStringLiteral("textDocumentSync"), 2);
    if (!capabilities.value(QStringLiteral("hoverProvider")).isObject())
        capabilities.insert(QStringLiteral("hoverProvider"), true);
    if (!capabilities.value(QStringLiteral("colorProvider")).isObject())
        capabilities.insert(QStringLiteral("colorProvider"), true);
    if (!capabilities.value(QStringLiteral("codeActionProvider")).isObject())
        capabilities.insert(QStringLiteral("codeActionProvider"), true);
    result.insert(QStringLiteral("capabilities"), capabilities);
    response->insert(QStringLiteral("result"), result);
}

} // namespace lsp
