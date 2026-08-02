#include "lspjsonrpc.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>

#include <cstdio>
#ifdef Q_OS_WIN
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

// Hands back whatever has arrived. Reading stdin through QFile asks for the
// whole buffer, and on Windows that waits until the buffer fills or the pipe
// closes: the proxy's request sat unread for the length of the test while this
// stub waited for 64 KiB that was never coming, and by the time the pipe
// closing let the read through there was nobody left to answer.
qint64 readAvailable(char *buffer, qsizetype size)
{
#ifdef Q_OS_WIN
    return _read(_fileno(stdin), buffer, static_cast<unsigned int>(size));
#else
    return ::read(fileno(stdin), buffer, static_cast<size_t>(size));
#endif
}

void send(QFile *output, const QJsonObject &message)
{
    const QByteArray framed = lsp::JsonRpcFramer::frame(message);
    const qint64 written = output->write(framed);
    const bool flushed = output->flush();
    lsp::trace(
        "fake qmlls sent",
        QStringLiteral("%1 of %2 flushed %3")
            .arg(written)
            .arg(framed.size())
            .arg(flushed));
}

QJsonObject response(const QJsonObject &request, const QJsonValue &result)
{
    return {
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), request.value(QStringLiteral("id"))},
        {QStringLiteral("result"), result},
    };
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const bool receivedShimArgument =
        QCoreApplication::arguments().contains(QStringLiteral("--loom-shim-test"));
    // The proxy under test frames by byte count, so this end of the pipe has to
    // leave the Windows text mode behind exactly as the real one does.
    lsp::useBinaryStdio();
    QFile output;
    if (!output.open(stdout, QIODevice::WriteOnly, QFileDevice::DontCloseHandle))
        return 1;

    lsp::trace("fake qmlls waiting");
    lsp::JsonRpcFramer framer;
    QByteArray buffer(64 * 1024, Qt::Uninitialized);
    while (true) {
        const qint64 read = readAvailable(buffer.data(), buffer.size());
        lsp::trace("fake qmlls read", QString::number(read));
        if (read <= 0)
            break;
        const QByteArray bytes(buffer.constData(), static_cast<qsizetype>(read));
        const auto messages = framer.push(bytes);
        for (const auto &message : messages) {
            const QString method = message.value(QStringLiteral("method")).toString();
            lsp::trace("fake qmlls message", method);
            if (method == QStringLiteral("initialize")) {
                send(
                    &output,
                    response(
                        message,
                        QJsonObject{
                            {QStringLiteral("serverInfo"),
                             QJsonObject{
                                 {QStringLiteral("name"),
                                  receivedShimArgument
                                      ? QStringLiteral("fake-qmlls-forwarded")
                                      : QStringLiteral("fake-qmlls")},
                             }},
                            {QStringLiteral("capabilities"),
                             QJsonObject{
                                 {QStringLiteral("positionEncoding"),
                                  QStringLiteral("utf-16")},
                                 {QStringLiteral("completionProvider"),
                                  QJsonObject{
                                      {QStringLiteral("triggerCharacters"),
                                       QJsonArray{QStringLiteral(".")}},
                                  }},
                                 {QStringLiteral("hoverProvider"), true},
                                 {QStringLiteral("textDocumentSync"), 2},
                             }},
                        }));
            } else if (method == QStringLiteral("textDocument/didOpen")) {
                const QString uri = message.value(QStringLiteral("params"))
                                        .toObject()
                                        .value(QStringLiteral("textDocument"))
                                        .toObject()
                                        .value(QStringLiteral("uri"))
                                        .toString();
                send(
                    &output,
                    QJsonObject{
                        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                        {QStringLiteral("method"),
                         QStringLiteral("textDocument/publishDiagnostics")},
                        {QStringLiteral("params"),
                         QJsonObject{
                             {QStringLiteral("uri"), uri},
                             {QStringLiteral("diagnostics"),
                              QJsonArray{QJsonObject{
                                  {QStringLiteral("range"),
                                   QJsonObject{
                                       {QStringLiteral("start"),
                                        QJsonObject{
                                            {QStringLiteral("line"), 0},
                                            {QStringLiteral("character"), 0},
                                        }},
                                       {QStringLiteral("end"),
                                        QJsonObject{
                                            {QStringLiteral("line"), 0},
                                            {QStringLiteral("character"), 1},
                                        }},
                                   }},
                                  {QStringLiteral("severity"), 3},
                                  {QStringLiteral("source"), QStringLiteral("qmlls")},
                                  {QStringLiteral("message"),
                                   QStringLiteral("fake QML diagnostic")},
                              }}},
                         }},
                    });
            } else if (method == QStringLiteral("textDocument/completion")) {
                send(
                    &output,
                    response(
                        message,
                        QJsonArray{QJsonObject{
                            {QStringLiteral("label"), QStringLiteral("qmlItem")},
                            {QStringLiteral("kind"), 10},
                        }}));
            } else if (
                method == QStringLiteral("textDocument/documentColor")
                || method == QStringLiteral("textDocument/codeAction")
                || method == QStringLiteral("textDocument/colorPresentation")) {
                send(&output, response(message, QJsonArray{}));
            } else if (method == QStringLiteral("textDocument/hover")) {
                send(&output, response(message, QJsonValue::Null));
            } else if (method == QStringLiteral("shutdown")) {
                send(&output, response(message, QJsonValue::Null));
            } else if (method == QStringLiteral("exit")) {
                return 0;
            } else if (message.contains(QStringLiteral("id"))) {
                send(&output, response(message, QJsonValue::Null));
            }
        }
    }
    return 0;
}
