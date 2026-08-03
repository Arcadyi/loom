# Development protocol

The wire format between `loom dev` (server) and the runtime inside the application
(client). Specified here because `loom::Runtime` is a **static** library: the runtime is
baked into an application at link time, so an upgraded CLI genuinely does meet an older
runtime, and the two have to agree about what is on the wire.

Transport is TCP on `127.0.0.1` only, on an ephemeral port chosen at startup. The runtime
refuses a non-loopback `LOOM_DEV_HOST`: a bundle is executable QML, so it is only ever
accepted from this machine.

---

## Framing

```
+--------+--------+--------+--------+--------+=================+
|             length (4)            |  type  |    payload      |
+--------+--------+--------+--------+--------+=================+
```

- **length** — 4 bytes, big-endian, unsigned. Counts the type byte plus the payload, so a
  payload of *n* bytes gives a length of *n + 1*. Zero is invalid.
- **type** — 1 byte, one of the message types below. An unrecognised value is a framing
  error.
- **payload** — `length - 1` bytes.

There is **no resynchronisation marker**. A framing error is therefore fatal to the
connection: the only correct response is to drop it. Both sides do. Discarding bytes to
"recover" would silently throw away whatever valid frames were pipelined behind the bad one.

---

## Messages

| Type | Value | Direction | Payload |
| --- | --- | --- | --- |
| `Hello` | 1 | client → server | JSON |
| `Bundle` | 2 | server → client | CBOR |
| `ReloadResult` | 3 | client → server | JSON |
| `Error` | 4 | either | JSON |
| `Ping` | 5 | either | empty |
| `Design` | 6 | server → client | CBOR |
| `StyleEdit` | 7 | client → server | CBOR |

### Capability negotiation

`ProtocolVersion` did not move for `StyleEdit`, because it does not have to: the frame is
client → server only, so a server that predates it never sends one and an older *client*
never receives one. The reverse direction is the problem. An unrecognised type is a framing
error and framing errors are fatal, so a **newer runtime** that sent `StyleEdit` to an older
server would have its connection dropped and lose hot reload for the rest of the session —
and that pairing is ordinary, not exotic: the `loom` CLI from a package and an application
built against a checkout declare the same `ProtocolVersion` and are different builds.

So the server states what it accepts, in an optional `capabilities` array on every `Bundle`:

```
{ "version": 2, "id": "…", "files": [ … ], "capabilities": ["styleEdit"] }
```

A decoder that predates the field reads by key and ignores it. A runtime that finds no
capabilities offers no source editing. Bumping the version instead would have stranded every
already-built application until it was rebuilt, because `loom::Runtime` is statically linked.

### `Hello`

Sent by the client immediately on connect. Nothing else is accepted before it.

```json
{
  "version": 1,
  "token": "<64 hex characters>",
  "applicationId": "MyApp",
  "currentBundle": "<bundle id, or empty>"
}
```

The server checks `version` first, then compares `token` in constant time. `currentBundle`
lets a reconnecting application say what it already has, so the server can skip resending a
bundle it would have sent identically.

A version mismatch gets an `Error` frame naming both versions before the connection closes —
the case this document exists for.

### `Bundle`

CBOR:

```
{
  "version": 1,
  "id":      "<hex>",
  "files":   [ { "path": ..., "contents": ..., "sha256": ... }, ... ]
}
```

`id` is a content hash: 32 hex characters over every path and file hash in the bundle. It is
the cache key, so identical content always yields the same id and a re-sent bundle is a
no-op.

Each file carries its own SHA-256, checked on arrival. `path` must start with `qt/qml/` and
must not be absolute, contain `..` or `.` segments, or contain a backslash.

The bundle includes the module's generated `qmldir`, with the `prefer` and `typeinfo`
directives removed — `prefer` would redirect the engine to the compiled-in copy and silently
disable reload.

### `Design`

CBOR:

```
{
  "version": 1,
  "path":    "<the design file's absolute path in the project>",
  "tokens":  <the raw JSON document, as bytes>
}
```

Sent on connect and on every save of the file named by `loom.json`'s `design` key. The
runtime applies it to the token registry, which lives in C++ that outlives every reload —
so this repaints the running window **without recreating the scene**. Nothing on screen
loses its state.

`path` travels with the document because the runtime never writes it to disk at the
location it belongs to. A relative `iconRoot` resolves against `path`; resolving it against
wherever the bytes were staged pointed every icon at a directory nothing can open.

A document that is not valid JSON changes nothing and leaves the previous tokens live —
which matters, because a file is malformed for most of the time someone is typing in it.

### `StyleEdit`

Sent by the runtime's development inspector to rewrite one `Lo.style` literal in the
project's source. The only frame that flows client → server carrying a change.

```
{ "version": 2, "path": "qt/qml/com/example/App/Main.qml",
  "line": 12, "column": 5,
  "oldStyle": "bg-surface", "newStyle": "bg-accent rounded-lg" }
```

`path` is the file's bundle resource path, byte-identical to a `Bundle` entry's `path`, so
the server resolves it by looking up what it put in the bundle rather than by reconstructing
a filesystem path. That is also the containment: a path the server never bundled resolves to
nothing, so an edit cannot name a file outside the project's QML roots however it is spelled.

`line` and `column` address the **item's declaration**, not the object, taken from `QQmlData`.
That works for the items with no `id` — most styled items — and maps directly onto an AST
node. Several instances of one delegate share a declaration site, which is the correct
semantics: there is one place in the source, and rewriting it once is what the user means.

`oldStyle` is what the running scene believes the literal says. The server refuses the edit
when the file no longer matches, rather than overwriting a change made in an editor
meanwhile.

**There is no success frame.** A successful edit changes a file the server is already
watching, so the ordinary rebuild produces a `Bundle` and the scene updates through the
normal reload path — exactly as if the change had been typed in an editor. A refusal comes
back as `Error`, and does *not* drop the connection.

The server refuses, rather than guesses, when:

- the binding is not a single string literal. A ternary or a concatenation cannot be
  rewritten, because the inspector reports the *evaluated* result and cannot say which branch
  produced it. Guessing would write a silent bug into the user's source;
- `newStyle` contains a class the compiler does not recognise, checked with the same
  `unknownStyleClasses()` the linter uses, so the inspector can never write a file that
  `loom lint` would then reject;
- the literal no longer says `oldStyle`;
- nothing is declared at that position, or what is there has no `Lo.style`.

The write itself is atomic (`QSaveFile`), so a crash mid-edit cannot leave half a document.

### `ReloadResult`

```json
{ "success": true, "kind": "bundle", "bundleId": "...", "message": "Reloaded" }
```

`kind` is `"bundle"` or `"design"`, so `loom dev` can report a design reload as its own
thing rather than claiming a bundle it never sent. On failure, `message` carries the QML
errors, which `loom dev` prints.

### `Error`

```json
{ "message": "..." }
```

Server to client: protocol version mismatch, or a bundle too large to send. Client to
server: QML warnings raised at run time, so they surface in the terminal running
`loom dev` rather than only in the application's own output.

### `Ping`

Empty payload, answered with an empty `Ping`. The server pings authenticated clients every
5 seconds and drops any that has said nothing for 20; the client treats 30 seconds of server
silence as a dead link and reconnects.

This exists because a half-open connection — a suspended host, a hung process — is
indistinguishable from an idle one at the TCP level. Without it, `loom dev` reports
"application connected" while every reload goes nowhere.

---

## Limits

| Limit | Value | Why |
| --- | --- | --- |
| Frame size | 64 MiB | Upper bound on a single bundle. |
| Pre-authentication frame size | 8 KiB | Frame length is read before the type is known, so without a separate limit an unauthenticated peer can make the receiver buffer a declared 64 MiB. A handful of sockets would exhaust memory before anyone proved who they were. |
| Authentication deadline | 2 s | A client that has not proved itself by then is dropped. |
| Concurrent clients | 8 | Past this, connections are refused rather than queued, so a connect loop cannot grow the client table. |
| Bundle files | 50 000 | Bounds per-file bookkeeping; the frame cap already bounds total bytes. |
| Scene state | 1 MiB | What `loomSaveState()` may return. |
| Design document | 1 MiB | Design files are hand-written configuration, not assets; anything approaching this is a mistake worth reporting rather than applying. |

---

## Compatibility

`ProtocolVersion` is **2** and should not be bumped unless the frame layout changes.

Because the runtime is statically linked, bumping it strands every already-built
application: they would all fail the `Hello` check until rebuilt. Additive changes — a new
message type an older peer can ignore, a new optional field — do not need a bump. `Error`
was added to the wire this way, and so was `Design`.

| Version | Shipped in | Change |
| --- | --- | --- |
| 1 | 0.1.0 | the original five messages; `Design` added additively in 0.2.0 |
| 2 | 0.2.1 | the `Design` payload became a CBOR map carrying the document's project path alongside the tokens |

The v2 change reshaped an existing frame rather than adding one, which is exactly what the
version gates. Without the bump a mismatched pair would have handshaked successfully and
then failed on the first design reload with a parse error — the confusing middle ground
versioning exists to avoid. With it, the mismatch is reported at the handshake, by name,
before the connection closes.

---

## Security

- Loopback only, on both sides.
- A 256-bit token per session from the OS CSPRNG, compared in constant time.
- Every file hash validated; traversal paths rejected.
- Pre-authentication frames capped, with a deadline and a client limit.

Development connection settings reach the application through the environment
(`LOOM_DEV_HOST`, `LOOM_DEV_PORT`, `LOOM_DEV_TOKEN`) and are never compiled into
release resources. Do not expose a development endpoint to an untrusted network.
