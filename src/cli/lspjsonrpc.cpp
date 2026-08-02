#include "lspjsonrpc.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QRegularExpression>
#include <QTextStream>
#include <QtGlobal>
#ifdef Q_OS_WIN
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#endif

namespace lsp {

namespace {
constexpr qsizetype MaximumMessageBytes = 16 * 1024 * 1024;
}

QList<QJsonObject> JsonRpcFramer::push(const QByteArray &bytes, QString *error)
{
    m_buffer.append(bytes);
    QList<QJsonObject> messages;

    while (true) {
        const qsizetype headerEnd = m_buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            if (m_buffer.size() > 8192) {
                if (error)
                    *error = QStringLiteral("LSP header exceeds 8192 bytes");
                m_buffer.clear();
            }
            break;
        }

        const QByteArray header = m_buffer.first(headerEnd);
        static const QRegularExpression contentLength(
            QStringLiteral("(?:^|\\r\\n)Content-Length\\s*:\\s*([0-9]+)\\s*(?:\\r\\n|$)"),
            QRegularExpression::CaseInsensitiveOption);
        const auto match = contentLength.match(QString::fromLatin1(header));
        if (!match.hasMatch()) {
            if (error)
                *error = QStringLiteral("LSP message has no valid Content-Length header");
            m_buffer.remove(0, headerEnd + 4);
            continue;
        }

        bool lengthOk = false;
        const qlonglong parsedLength = match.captured(1).toLongLong(&lengthOk);
        if (!lengthOk || parsedLength < 0 || parsedLength > MaximumMessageBytes) {
            if (error)
                *error = QStringLiteral("LSP Content-Length is out of range");
            m_buffer.clear();
            break;
        }
        const qsizetype bodyLength = qsizetype(parsedLength);
        const qsizetype frameLength = headerEnd + 4 + bodyLength;
        if (m_buffer.size() < frameLength)
            break;

        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(
            m_buffer.sliced(headerEnd + 4, bodyLength), &parseError);
        m_buffer.remove(0, frameLength);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            if (error) {
                *error = QStringLiteral("invalid LSP JSON: %1 at offset %2")
                             .arg(parseError.errorString())
                             .arg(parseError.offset);
            }
            continue;
        }
        messages.append(document.object());
    }
    return messages;
}

QByteArray JsonRpcFramer::frame(const QJsonObject &message)
{
    const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    return QByteArrayLiteral("Content-Length: ") + QByteArray::number(body.size())
        + QByteArrayLiteral("\r\n\r\n") + body;
}

QString requestIdKey(const QJsonValue &id)
{
    return QString::fromUtf8(
        QJsonDocument(QJsonArray{id}).toJson(QJsonDocument::Compact));
}

void trace(const char *event, const QString &detail)
{
    static const QString path = qEnvironmentVariable("LOOM_LSP_TRACE");
    if (path.isEmpty())
        return;
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    QTextStream(&file) << QCoreApplication::applicationPid() << ' '
                       << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << ' '
                       << event << (detail.isEmpty() ? QString() : ' ' + detail) << '\n';
}

void useBinaryStdio()
{
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

} // namespace lsp
