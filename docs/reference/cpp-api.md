# C++ API

Everything loom installs under `include/loom/`. Five headers, and most
applications use one of them.

| Header | Target | Contents |
| --- | --- | --- |
| [`<loom/loom.h>`](#loomloomh) | `loom::loom` | themes, config loading, icon root |
| [`<loom/loomcatalogue.h>`](#loomloomcatalogueh) | `loom::loom` | the utility vocabulary, for tooling |
| [`<loom/application.h>`](#loomapplicationh) | `loom::Runtime` | the application entry point |
| [`<loom/reloadcontroller.h>`](#loomreloadcontrollerh) | `loom::Runtime` | scene loading and hot reload, for embedders |
| [`<loom/protocol.h>`](#loomprotocolh) | `loom::Protocol` | the dev-server wire format |

Linking is covered in [../tooling/cmake.md](../tooling/cmake.md). Styling only
needs `loom::loom` and `loom::loomplugin`; hot reload adds `loom::Runtime`,
which pulls in `loom::Protocol`.

Everything is in namespace `loom`. The library is C++20 and static.

---

## `<loom/loom.h>`

The styling library's public surface. Available whether or not you use the
runtime half.

```cpp
namespace loom {

enum class ThemeMode { Explicit, System };

const char *version();

void setTheme(const QString &name);
QString theme();
void setThemeMode(ThemeMode mode);
ThemeMode themeMode();

bool loadConfig(const QString &filePath);
bool reloadConfig(const QString &filePath);
bool reloadConfigData(const QByteArray &json, const QString &basePath);

void setIconRoot(const QUrl &root);
QUrl iconRoot();

}
```

### `version()`

The library version, e.g. `"0.4.0"`. Compiled in, so it reports the library you
linked against rather than the headers you built with.

### `setTheme()` / `theme()`

Switches the active theme. Every typed token binding re-evaluates and every
`Lo.style` that resolves differently re-applies; only properties whose values
changed are written.

An unknown name warns on the `loom.tokens` category and leaves the theme
unchanged, rather than resolving every semantic colour to nothing. `"light"` and
`"dark"` are built in; a [design token file](../styling/configuration.md) can
add more.

### `setThemeMode()` / `themeMode()`

`ThemeMode::System` follows the operating system color scheme and selects the
design's `theme.light` or `theme.dark` mapping. `setTheme()` switches back to
`ThemeMode::Explicit`. The equivalent QML properties are `Loom.themeMode` and
`Loom.theme`.

### `loadConfig()` / `reloadConfig()`

Both read a JSON [design token file](../styling/configuration.md). They differ
in one way:

- **`loadConfig()` merges** into what is already defined. Use it at startup.
- **`reloadConfig()` replaces**: tokens reset to the built-in set first, so a
  token the file no longer defines stops resolving.

Both return `false` when the file cannot be read or parsed, and in that case
change nothing at all — which matters for reload, because a file is malformed
for most of the time someone is typing in it.

Call either before the engine loads the scene for a flicker-free start, or at
any later point.

On a reload the **currently active explicit theme wins** over `theme.default`
unless it no longer exists. System theme mode remains system mode and uses the
reloaded light/dark mappings.

### `reloadConfigData()`

As `reloadConfig()`, for a document that is not on disk at the location it
belongs to:

```cpp
loom::reloadConfigData(jsonBytes, "/path/to/project/design/tokens.json");
```

A relative `iconRoot` inside the document resolves against `basePath` rather
than against wherever the bytes came from. `loom dev` uses this: it receives the
design tokens over a socket, and resolving icons against a socket has no
meaning.

### `setIconRoot()` / `iconRoot()`

The base URL a relative `Loom.icon("home.svg")` resolves against. Also settable
from QML as `Loom.iconRoot`, and from a design file's `iconRoot` key, where it
is relative to that file.

Set it **before the UI loads**: `icon()` bindings track the colour argument, not
the root, so a later change does not repaint icons already on screen.

---

## `<loom/loomcatalogue.h>`

The `Lo.style` vocabulary, enumerated for tooling — editor completion, docs
generation, and the checker behind `loom style --check`.

```cpp
namespace loom {

struct StyleUtilityFamily {
    QString prefix;      // "bg-"
    QString scale;       // "color", for display
    QStringList values;  // every valid key, sorted
};

struct StyleCatalogue {
    QString version;
    QString theme;               // active theme at generation time
    QStringList variants;
    QStringList classes;         // every enumerable class, no variant prefixes
    QList<StyleUtilityFamily> families;
    QStringList numericPrefixes; // prefixes also taking a bare number
};

StyleCatalogue styleCatalogue();
QByteArray styleCatalogueJson();
QStringList unknownStyleClasses(const QString &style);

}
```

Everything here derives from the parser's own tables and the live token
registry, never from a second hand-maintained list. A utility or token added to
loom appears here without another edit, and a test asserts that every class the
catalogue emits actually parses.

Two consequences:

- the catalogue reflects the **current registry contents**, so a config that
  defines extra colours widens it — dump it after loading the same config your
  application uses;
- it includes token names introduced by **every configured theme**, so tooling
  can validate and complete `theme-name:` rules before that theme is active.
  `theme` records which theme was active when the catalogue was produced; it
  does not narrow the vocabulary.

`numericPrefixes` covers families that additionally accept a bare number, which
cannot be enumerated: `border-2`, `border-0.5`. Completion should offer the
enumerated families and let anything numeric through.

`unknownStyleClasses()` runs the same parse as the real compiler, without
warning or caching, so a checker can report per occurrence rather than once per
unique string.

---

## `<loom/application.h>`

The entry point. A generated `main.cpp` is the whole of the API most
applications need:

```cpp
#include <loom/application.h>

int main(int argc, char *argv[])
{
    loom::Application application(argc, argv);
#ifndef NDEBUG
    application.enableDevelopmentRuntime();
#endif
    return application.run(
        QStringLiteral(LOOM_APP_URI), QStringLiteral(LOOM_APP_ENTRY));
}
```

`LOOM_APP_URI` and `LOOM_APP_ENTRY` are defined by `loom_enable_hot_reload`, so
the module URI has one definition rather than one in CMake and another in C++.

`Application` owns a `QGuiApplication` and a `QQmlApplicationEngine`. It is
non-copyable.

### `setEngineInitializer(EngineInitializer)`

```cpp
using EngineInitializer = std::function<void(QQmlApplicationEngine &)>;
```

Runs against the engine immediately before the root scene is created. This is
where C++ types, context properties and import paths belong.

**Anything registered here survives hot reload.** Only the QML scene is rebuilt;
the engine and every C++ object outlive it. That is the whole point of the
split — a reload keeps your services, your connections and your model state.
Changing the C++ itself needs a rebuild, which `loom dev` performs
automatically.

### `enableDevelopmentRuntime(bool = true)`

Allows the runtime to connect to a development server. Without this the
application never opens a socket, whatever `LOOM_DEV_*` says in the environment.

The generated `main.cpp` gates it on `#ifndef NDEBUG`, so a Release,
RelWithDebInfo or MinSizeRel build has **no reload path compiled in at all** —
not a disabled one. That is the production boundary; see
[architecture.md](architecture.md).

Development builds also install the Ctrl+Shift+I style inspector. It identifies
the styled item under the pointer and displays its raw class string, active
theme and states, and resolved property writes. Click to lock the selection. It
is absent from production builds with the rest of the development runtime.

### `run(moduleUri, entryType)`

Loads `entryType` from `moduleUri`, connects the development runtime if it was
enabled, and runs the event loop.

If `loom_add_application` was given a `DESIGN` file, its compiled-in copy loads
first — before the engine initializer and before the scene — so the first frame
is already themed. Under `loom dev` the on-disk file supersedes it on every
save.

Returns the event loop's exit code, or `1` if the scene could not be loaded, in
which case the QML errors are printed to stderr.

### `guiApplication()` / `engine()`

The owned objects, for anything this wrapper does not cover. Prefer
`setEngineInitializer()` for setup that must happen before the scene loads.

---

## `<loom/reloadcontroller.h>`

Use this directly only when embedding loom in an application that owns its own
engine. `loom::Application` drives it for you.

```cpp
namespace loom {

class ReloadController final : public QObject {
public:
    explicit ReloadController(QQmlApplicationEngine &engine, QObject *parent = nullptr);

    bool load(const QString &moduleUri, const QString &entryType);
    void connectToDevelopmentServer(const QString &host, quint16 port, const QString &token);

    bool applyBundle(const QByteArray &payload, QString *error = nullptr);
    bool applyDesign(const QByteArray &payload, QString *error = nullptr);

    QObject *rootObject() const;
    QString lastError() const;

signals:
    void sceneReloaded(const QString &bundleId);
    void reloadFailed(const QString &message);
    void designReloaded();
};

}
```

### `load()`

Loads `entryType` from the compiled-in resources of `moduleUri`. Returns `false`
if the scene could not be created; the reason is in `lastError()`.

### `connectToDevelopmentServer()`

Connects and authenticates. Reconnects with bounded backoff if the connection
drops or the server stops responding — 250 ms doubling to 8 s, ten attempts,
after which hot reload is off for the life of the process.

**`host` must be a loopback address.** A non-loopback host is refused outright.
The dev server executes received QML, so accepting one from off-machine would be
a remote code execution primitive; see
[protocol.md](protocol.md).

### `applyBundle()`

Validates and applies a bundle, replacing the running scene.

The incoming scene is **compiled before the running one is touched**, so a
bundle that does not compile leaves the live scene alone and costs nothing. If
it compiles but fails to construct, the controller rolls back to the last scene
that constructed successfully. A bundle whose id matches the running one is a
no-op.

### `applyDesign()`

Applies design tokens to the process-wide registry, replacing rather than
merging. `payload` is an encoded `loom::Design` — the token document plus the
path it has in the project, which a relative `iconRoot` resolves against. The
bytes never reach the filesystem.

**The scene is not involved.** Tokens live in C++ that outlives every reload, so
this repaints the running window instead of rebuilding it, and nothing on screen
loses its state. Malformed JSON, or more than `MaximumDesignSize` bytes, changes
nothing.

### Bundle storage

Bundles are written to a cache directory private to the controller, removed on
destruction. Directories left behind by processes that are gone are swept on the
next start, so a crash does not accumulate them.

---

## `<loom/protocol.h>`

The framed transport between `loom dev` and the runtime. You need this only to
implement one side of the protocol; the wire format is documented in
[protocol.md](protocol.md).

```cpp
namespace loom {

inline constexpr quint16 ProtocolVersion = 2;
inline constexpr qsizetype MaximumFrameSize = 64 * 1024 * 1024;
inline constexpr qsizetype MaximumPreAuthFrameSize = 8 * 1024;
inline constexpr qsizetype MaximumStateSize = 1024 * 1024;
inline constexpr qsizetype MaximumBundleFiles = 50000;
inline constexpr qsizetype MaximumDesignSize = 1024 * 1024;

enum class MessageType : quint8 {
    Hello = 1, Bundle = 2, ReloadResult = 3, Error = 4, Ping = 5, Design = 6,
};

struct Frame  { MessageType type; QByteArray payload; };
struct Bundle { QString id; QList<BundleFile> files; };
struct Design { QString path; QByteArray tokens; };

QByteArray encodeFrame(MessageType type, const QByteArray &payload);
bool takeFrame(QByteArray &buffer, Frame &frame, QString *error = nullptr,
               qsizetype maximumFrameSize = MaximumFrameSize);

QByteArray encodeBundle(const Bundle &bundle);
bool decodeBundle(const QByteArray &payload, Bundle &bundle, QString *error = nullptr);

QByteArray encodeDesign(const Design &design);
bool decodeDesign(const QByteArray &payload, Design &design, QString *error = nullptr);

bool isSafeBundlePath(const QString &path);

}
```

`takeFrame()` returns `false` both when no complete frame has arrived yet
(`*error` stays empty) and when the stream is unusable (`*error` is set). There
is no resynchronisation marker in the framing, so **a set error is always
fatal** — the caller must drop the connection rather than try to recover.

`ProtocolVersion` gates compatibility. Bumping it strands every already-built
application until it is rebuilt, because `loom::Runtime` is statically linked.
Additive changes — a new message type an older peer ignores, a new optional
field — do not need one.
