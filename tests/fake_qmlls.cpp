#include "lspjsonrpc.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>

namespace {

void send(QFile *output, const QJsonObject &message)
{
    output->write(lsp::JsonRpcFramer::frame(message));
    output->flush();
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
    QFile input;
    QFile output;
    if (!input.open(stdin, QIODevice::ReadOnly, QFileDevice::DontCloseHandle)
        || !output.open(stdout, QIODevice::WriteOnly, QFileDevice::DontCloseHandle)) {
        return 1;
    }

    lsp::JsonRpcFramer framer;
    while (true) {
        const QByteArray bytes = input.read(64 * 1024);
        if (bytes.isEmpty())
            break;
        const auto messages = framer.push(bytes);
        for (const auto &message : messages) {
            const QString method = message.value(QStringLiteral("method")).toString();
            if (method == QStringLiteral("initialize")) {
                send(
                    &output,
                    response(
                        message,
                        QJsonObject{
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
