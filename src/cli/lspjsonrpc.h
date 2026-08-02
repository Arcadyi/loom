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

// Takes stdin and stdout out of the Windows C runtime's text mode, and does
// nothing anywhere else. Every process speaking the framing above has to call
// this before it reads or writes a byte: text mode grows each \n written into
// \r\n and shrinks each \r\n read back into \n, so Content-Length stops
// describing the bytes on the wire and the \r\n\r\n ending a header never
// survives the trip. Neither end can then find a message boundary.
void useBinaryStdio();

// A language server has no console to complain to -- stdout carries the
// protocol and an editor rarely shows stderr -- so when one goes quiet there is
// nothing to look at. Point LOOM_LSP_TRACE at a file to find out how far it
// got. Every process writing there tags its lines with its own id, since a
// proxy and the server behind it can be running at once. Costs one environment
// lookup when unset.
void trace(const char *event, const QString &detail = {});

} // namespace lsp
