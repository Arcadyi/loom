#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

/// Framed transport between `loom dev` and the runtime inside an application.
/// A frame is a 4-byte big-endian length, a 1-byte type, then the payload; the
/// length counts the type byte. There is no resynchronisation marker, so a
/// framing error is fatal to the connection.
///
/// \sa docs/protocol.md
namespace loom {

/// Bumping this strands every already-built application until it is rebuilt,
/// because loom::Runtime is statically linked. Additive changes -- a new
/// message type an older peer ignores, a new optional field -- do not need one.
inline constexpr quint16 ProtocolVersion = 1;
inline constexpr qsizetype MaximumFrameSize = 64 * 1024 * 1024;
// A peer that has not authenticated may not declare a frame larger than this.
// Frame length is read before the type is known, so without a separate limit an
// unauthenticated connection can make the receiver buffer a declared 64 MiB,
// and a handful of sockets exhaust memory before anyone proves who they are.
inline constexpr qsizetype MaximumPreAuthFrameSize = 8 * 1024;
// Largest scene state loomSaveState() may return; anything larger is discarded
// and the scene reloads clean.
inline constexpr qsizetype MaximumStateSize = 1024 * 1024;
// Bounds the per-file bookkeeping a single bundle can force. The frame cap
// already bounds total bytes; this bounds the number of allocations.
inline constexpr qsizetype MaximumBundleFiles = 50000;

enum class MessageType : quint8 {
    Hello = 1,
    Bundle = 2,
    ReloadResult = 3,
    Error = 4,
    Ping = 5,
};

bool isKnownMessageType(quint8 value);

struct Frame {
    MessageType type{};
    QByteArray payload;
};

struct BundleFile {
    QString path;
    QByteArray contents;
    QByteArray sha256;
};

struct Bundle {
    QString id;
    QList<BundleFile> files;
};

QByteArray encodeFrame(MessageType type, const QByteArray &payload);
// Returns false both when no complete frame has arrived yet (*error stays
// empty) and when the stream is unusable (*error is set). There is no resync
// marker in the framing, so a set error is always fatal: the caller must drop
// the connection rather than try to recover.
bool takeFrame(
    QByteArray &buffer, Frame &frame, QString *error = nullptr,
    qsizetype maximumFrameSize = MaximumFrameSize);

QByteArray encodeBundle(const Bundle &bundle);
bool decodeBundle(const QByteArray &payload, Bundle &bundle, QString *error = nullptr);

bool isSafeBundlePath(const QString &path);

} // namespace loom
