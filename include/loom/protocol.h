#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

/// Framed transport between `loom dev` and the runtime inside an application.
/// A frame is a 4-byte big-endian length, a 1-byte type, then the payload; the
/// length counts the type byte. There is no resynchronisation marker, so a
/// framing error is fatal to the connection.
///
/// \sa docs/reference/protocol.md
namespace loom {

/// Bumping this strands every already-built application until it is rebuilt,
/// because loom::Runtime is statically linked. Additive changes -- a new
/// message type an older peer ignores, a new optional field -- do not need one.
///
/// v2 (loom 0.2.1): the Design payload gained the document's project path, so
/// a relative `iconRoot` can resolve against the project rather than against
/// wherever the bytes were staged. That reshaped an existing frame rather than
/// adding one, so a mismatched peer must be told plainly at the handshake
/// instead of failing later with a parse error on one message type.
inline constexpr quint16 ProtocolVersion = 2;
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
    // Design token JSON plus the path it came from, applied in place without
    // rebuilding the scene.
    Design = 6,
    // Client to server only: rewrite one `Lo.style` literal in the project's
    // source. The reply is the ordinary Bundle the file watcher produces, or an
    // Error frame -- both already exist, so no server-to-client type is added
    // and ProtocolVersion stays where it is.
    StyleEdit = 7,
};

// Optional capability names a server advertises in its Bundle frames. A runtime
// only sends StyleEdit after seeing the matching capability, because takeFrame()
// treats an unknown type as a *fatal* framing error: a new runtime meeting an
// older server -- same declared ProtocolVersion, different build, which happens
// whenever the CLI comes from a package and the application from a checkout --
// would otherwise have its hot-reload session dropped for the rest of the run.
inline constexpr char CapabilityStyleEdit[] = "styleEdit";

// Largest design token document the server will send or the runtime accept.
// Design files are hand-written configuration, not assets; anything approaching
// this is a mistake worth reporting rather than applying.
inline constexpr qsizetype MaximumDesignSize = 1024 * 1024;

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
    // Read by key, so a decoder that predates this field ignores it and a
    // decoder that postdates a server without it sees an empty list. That is
    // the whole negotiation.
    QStringList capabilities;
};

// One `Lo.style` literal, addressed by where its item is *declared* rather than
// by object identity. Line and column come from QQmlData, so this works for the
// items that have no `id` -- most styled items -- and it maps directly onto an
// AST node instead of needing a search. Repeated instances of one delegate share
// a declaration site, which is correct: rewriting the source once is what the
// user means.
struct StyleEdit {
    // The file's path within the bundle, identical to BundleFile::path, so the
    // server resolves it by lookup rather than by path arithmetic.
    QString path;
    int line = 0;
    int column = 0;
    // What the runtime believes the literal currently says. The server refuses
    // the edit when the file no longer matches, rather than overwriting a change
    // made in an editor meanwhile.
    QString oldStyle;
    QString newStyle;
};

// Bounds a single edit. Class strings are short; anything near this is not one.
inline constexpr qsizetype MaximumStyleEditSize = 64 * 1024;

QByteArray encodeFrame(MessageType type, const QByteArray &payload);
// Returns false both when no complete frame has arrived yet (*error stays
// empty) and when the stream is unusable (*error is set). There is no resync
// marker in the framing, so a set error is always fatal: the caller must drop
// the connection rather than try to recover.
bool takeFrame(
    QByteArray &buffer, Frame &frame, QString *error = nullptr,
    qsizetype maximumFrameSize = MaximumFrameSize);

struct Design {
    // Where the document lives in the project. The runtime resolves a relative
    // `iconRoot` against this rather than against the bytes' own location,
    // which is a staging directory -- without it, hot-reloading a design file
    // re-roots every relative icon to a path nothing can open. Sent alongside
    // the document because `loom.json` can retarget it mid-session.
    QString path;
    QByteArray tokens;
};

QByteArray encodeBundle(const Bundle &bundle);
bool decodeBundle(const QByteArray &payload, Bundle &bundle, QString *error = nullptr);

QByteArray encodeDesign(const Design &design);
bool decodeDesign(const QByteArray &payload, Design &design, QString *error = nullptr);

QByteArray encodeStyleEdit(const StyleEdit &edit);
bool decodeStyleEdit(const QByteArray &payload, StyleEdit &edit, QString *error = nullptr);

bool isSafeBundlePath(const QString &path);

} // namespace loom
