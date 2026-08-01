#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace lsp {

// Incremental parser for the Content-Length framed JSON-RPC stream used by
// LSP. Both editor stdin and qmlls stdout can split a header or body at any
// byte, or coalesce several messages into one read.
class JsonRpcFramer {
public:
    QList<QJsonObject> push(const QByteArray &bytes, QString *error = nullptr);
    static QByteArray frame(const QJsonObject &message);

private:
    QByteArray m_buffer;
};

QString requestIdKey(const QJsonValue &id);

} // namespace lsp
