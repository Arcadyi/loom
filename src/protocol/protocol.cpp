#include <loom/protocol.h>

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSet>
#include <QtEndian>

namespace loom {

bool isKnownMessageType(const quint8 value)
{
    switch (static_cast<MessageType>(value)) {
    case MessageType::Hello:
    case MessageType::Bundle:
    case MessageType::ReloadResult:
    case MessageType::Error:
    case MessageType::Ping:
    case MessageType::Design:
        return true;
    }
    return false;
}

QByteArray encodeFrame(const MessageType type, const QByteArray &payload)
{
    if (payload.size() + 1 > MaximumFrameSize)
        return {};
    const quint32 bodySize = static_cast<quint32>(payload.size() + 1);
    QByteArray frame;
    frame.resize(4);
    qToBigEndian(bodySize, reinterpret_cast<uchar *>(frame.data()));
    frame.append(static_cast<char>(type));
    frame.append(payload);
    return frame;
}

bool takeFrame(
    QByteArray &buffer, Frame &frame, QString *error, const qsizetype maximumFrameSize)
{
    if (buffer.size() < 4)
        return false;

    const auto bodySize =
        qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(buffer.constData()));
    // Deliberately not buffer.clear(): the framing has no resync marker, so
    // there is no way to find the next frame boundary. Clearing pretended to
    // recover while silently discarding whatever valid pipelined frames had
    // already arrived. A framing error is fatal to the connection.
    if (bodySize == 0 || bodySize > maximumFrameSize) {
        if (error) {
            *error = QStringLiteral("Invalid protocol frame size: %1 (limit %2)")
                         .arg(bodySize)
                         .arg(maximumFrameSize);
        }
        return false;
    }
    if (buffer.size() < static_cast<qsizetype>(bodySize) + 4)
        return false;

    const auto type = static_cast<quint8>(buffer.at(4));
    if (!isKnownMessageType(type)) {
        if (error)
            *error = QStringLiteral("Unknown protocol message type: %1").arg(type);
        return false;
    }

    frame.type = static_cast<MessageType>(type);
    frame.payload = buffer.sliced(5, bodySize - 1);
    buffer.remove(0, bodySize + 4);
    return true;
}

QByteArray encodeBundle(const Bundle &bundle)
{
    QCborMap root;
    root.insert(QStringLiteral("version"), ProtocolVersion);
    root.insert(QStringLiteral("id"), bundle.id);

    QCborArray files;
    for (const auto &file : bundle.files) {
        QCborMap encoded;
        encoded.insert(QStringLiteral("path"), file.path);
        encoded.insert(QStringLiteral("contents"), file.contents);
        const auto hash = file.sha256.isEmpty()
            ? QCryptographicHash::hash(file.contents, QCryptographicHash::Sha256)
            : file.sha256;
        encoded.insert(QStringLiteral("sha256"), hash);
        files.append(encoded);
    }
    root.insert(QStringLiteral("files"), files);
    return QCborValue(root).toCbor();
}

bool decodeBundle(const QByteArray &payload, Bundle &bundle, QString *error)
{
    QCborParserError parserError;
    const auto value = QCborValue::fromCbor(payload, &parserError);
    if (parserError.error != QCborError::NoError || !value.isMap()) {
        if (error)
            *error = QStringLiteral("Malformed bundle payload");
        return false;
    }

    const auto root = value.toMap();
    if (root.value(QStringLiteral("version")).toInteger() != ProtocolVersion) {
        if (error)
            *error = QStringLiteral("Unsupported bundle protocol version");
        return false;
    }

    const auto id = root.value(QStringLiteral("id"));
    const auto files = root.value(QStringLiteral("files"));
    static const QRegularExpression safeId(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
    if (!id.isString() || !safeId.match(id.toString()).hasMatch() || !files.isArray()) {
        if (error)
            *error = QStringLiteral("Bundle is missing its id or file list");
        return false;
    }

    const auto fileArray = files.toArray();
    if (fileArray.size() > MaximumBundleFiles) {
        if (error) {
            *error = QStringLiteral("Bundle declares %1 files, over the limit of %2")
                         .arg(fileArray.size())
                         .arg(MaximumBundleFiles);
        }
        return false;
    }

    Bundle decoded;
    decoded.id = id.toString();
    decoded.files.reserve(fileArray.size());
    qsizetype decodedBytes = 0;
    QSet<QString> seenPaths;
    for (const auto &item : fileArray) {
        if (!item.isMap()) {
            if (error)
                *error = QStringLiteral("Bundle contains an invalid file record");
            return false;
        }
        const auto map = item.toMap();
        BundleFile file{
            map.value(QStringLiteral("path")).toString(),
            map.value(QStringLiteral("contents")).toByteArray(),
            map.value(QStringLiteral("sha256")).toByteArray(),
        };
        if (!isSafeBundlePath(file.path)) {
            if (error)
                *error = QStringLiteral("Unsafe bundle path: %1").arg(file.path);
            return false;
        }
        if (seenPaths.contains(file.path)) {
            if (error)
                *error =
                    QStringLiteral("Bundle contains duplicate path: %1").arg(file.path);
            return false;
        }
        seenPaths.insert(file.path);
        decodedBytes += file.contents.size();
        if (decodedBytes > MaximumFrameSize) {
            if (error) {
                *error =
                    QStringLiteral("Bundle contents exceed the %1 byte protocol limit")
                        .arg(MaximumFrameSize);
            }
            return false;
        }
        const auto actualHash =
            QCryptographicHash::hash(file.contents, QCryptographicHash::Sha256);
        if (file.sha256.size() != 32 || actualHash != file.sha256) {
            if (error)
                *error = QStringLiteral("Bundle hash mismatch for %1").arg(file.path);
            return false;
        }
        decoded.files.append(std::move(file));
    }

    bundle = std::move(decoded);
    return true;
}

bool isSafeBundlePath(const QString &path)
{
    if (path.isEmpty() || path.startsWith(QLatin1Char('/'))
        || path.contains(QLatin1Char('\\'))) {
        return false;
    }
    const auto pieces = path.split(QLatin1Char('/'));
    if (pieces.contains(QStringLiteral("..")) || pieces.contains(QStringLiteral(".")))
        return false;
    return path.startsWith(QStringLiteral("qt/qml/"));
}

} // namespace loom
