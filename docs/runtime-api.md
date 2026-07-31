# Runtime API

`respin::Runtime` is a static library installed with its headers. A generated `src/main.cpp`
is the whole of the API most applications need:

```cpp
#include <respin/application.h>

int main(int argc, char *argv[])
{
    respin::Application application(argc, argv);
#ifndef NDEBUG
    application.enableDevelopmentRuntime();
#endif
    return application.run(
        QStringLiteral(RESPIN_APP_URI), QStringLiteral(RESPIN_APP_ENTRY));
}
```

`RESPIN_APP_URI` and `RESPIN_APP_ENTRY` are defined by `respin_enable_hot_reload`, so the
module URI has one definition rather than being repeated in C++.

---

## `respin::Application`

Owns a `QGuiApplication` and a `QQmlApplicationEngine`, loads the entry scene, and
optionally connects to a development server.

### `void enableDevelopmentRuntime(bool enabled = true)`

Allows the runtime to connect to a development server. Without it the application never
opens a socket, whatever the environment says.

The generated `main.cpp` gates this on `#ifndef NDEBUG`, so `Release`, `RelWithDebInfo` and
`MinSizeRel` builds have no reload path compiled in at all. `respin dev` warns when you
select such a configuration.

### `void setEngineInitializer(EngineInitializer initializer)`

`using EngineInitializer = std::function<void(QQmlApplicationEngine &)>;`

Called with the engine before the scene is loaded, and the place to register C++ types,
context properties, singletons and import paths:

```cpp
respin::Application application(argc, argv);
application.setEngineInitializer([](QQmlApplicationEngine &engine) {
    qmlRegisterType<Backend>("com.example.MyApp", 1, 0, "Backend");
    engine.rootContext()->setContextProperty("buildStamp", QStringLiteral(__DATE__));
});
```

C++ types registered here survive hot reload: only the QML scene is rebuilt, and the engine,
its registrations and every C++ object outlive it. Changing the C++ itself requires a
rebuild, which `respin dev` performs automatically when anything under `src/` changes.

### `int run(const QString &moduleUri, const QString &entryType)`

Loads `entryType` from `moduleUri`, connects the development runtime if enabled, and runs the
event loop. Returns the event loop's exit code, or `1` if the scene could not be loaded — in
which case the QML errors are printed.

### `QGuiApplication &guiApplication()` / `QQmlApplicationEngine &engine()`

Direct access, for anything the wrapper does not cover.

---

## Scene state across a reload

Reload destroys the root object and creates a new one. Most state carries across on its own:
every **QML-declared, writable property** of every object that has an `id` is captured and
written back, keyed by file name and id. Nothing needs annotating, and nothing needs routing
through the root.

```qml
// Survives a reload with no hooks at all.
Item {
    id: appRoot
    property string currentPage: "home"

    TextField { id: search }        // `search.text` is not declared in QML -- see below
    Item {
        id: filters
        property bool showArchived: false   // survives, without appRoot knowing it exists
    }
}
```

What automatic capture deliberately leaves alone:

| Not captured | Why |
| --- | --- |
| properties holding a binding | writing a stored value back would destroy the binding and freeze the property |
| `readonly` properties | not writable |
| objects with no `id` | nothing stable to key them by across the reload |
| a component's *root* object, unless the instantiation site names it | its id lives in the file it was declared in, but at runtime it resolves in the context that created it — `header: TopBar {}` is anonymous, `header: TopBar { id: topBar }` is captured as `Main.qml#topBar` |
| properties inherited from the C++ type (`width`, `text`, `color`) | the new scene sets these up itself; only what the QML file declared is carried |
| ids used by more than one live object (a `Repeater` delegate, a reused page) | the key is ambiguous, and restoring one instance's state into another is worse than restoring nothing |
| values with no JSON form | the old scene is about to be destroyed; see below |

Two files with the same base name in different directories share a key space, since the key
is the file name rather than the full path — bundles are unpacked to a new directory on every
reload, so the full URL is not stable enough to key on.

### Hooks, for what capture cannot reach

For anything in that table — a bound property, an object with no id, state that is not a
property — give the root object either or both of these functions. They run in addition to
automatic capture, and win wherever the two overlap:

```qml
function respinSaveState() {
    return { "route": currentRoute, "query": search.text }
}

function respinRestoreState(state) {
    currentRoute = state.route ?? "home"
    search.text = state.query ?? ""
}
```

Both are optional; a missing hook now means automatic capture alone rather than a clean
scene. `respinRestoreState` is looked up on the whole type hierarchy, so inheriting it from a
base QML type works.

### What survives

The returned value is **normalized through JSON** before the old scene is destroyed. This is
not incidental: the old value may hold pointers into the scene that is about to be deleted,
and handing those back would be a use-after-free. In practice:

| You return | You get back |
| --- | --- |
| numbers, strings, booleans, arrays, plain objects | the same |
| a `QObject` or a QML item | `null` |
| a `Date` | its string form |
| `undefined`, or something with no JSON form | nothing from the hook, with a warning; captured properties are unaffected |
| more than 1 MiB of state | nothing; the scene reloads clean, with a warning |

`respinRestoreState` is called **synchronously**, immediately after the new root is created
and before the first frame. A queued call would show one frame of un-restored state.

---

## What "last known good" means

If the incoming scene fails to construct, the runtime reloads the previous bundle, or the
compiled-in resources if there is no previous bundle.

"Last known good" means **constructed successfully** — not "runs without warnings". A bundle
that compiles and builds its object tree is accepted even if it then logs QML errors at run
time. Those errors are reported over the wire and appear in the `respin dev` terminal, but
they do not trigger a rollback: a warning means the scene is up and something in it
misbehaved, and rolling back on that would undo working reloads.

A bundle that does not **compile** never reaches this point. It is rejected before anything
is torn down, so the running scene is untouched — no teardown, no cleared caches, no
rollback. This is the common case: it is what happens every time a QML file is saved
mid-edit.

---

## `respin::ReloadController`

The engine bootstrap and reload machinery behind `Application`. Use it directly only when
you are not using `respin::Application` — for instance when embedding respin in an existing
application that owns its own engine.

```cpp
respin::ReloadController controller(engine);
controller.load(QStringLiteral("com.example.MyApp"), QStringLiteral("Main"));
controller.connectToDevelopmentServer(host, port, token);
```

| Member | Purpose |
| --- | --- |
| `bool load(moduleUri, entryType)` | Loads the compiled-in scene. `false` if it fails; see `lastError()`. |
| `void connectToDevelopmentServer(host, port, token)` | Connects, authenticates and reconnects with bounded backoff. |
| `bool applyBundle(payload, error)` | Applies a bundle directly. Used by tests and by the socket path. |
| `QObject *rootObject() const` | The live root scene. |
| `QString lastError() const` | The most recent failure. |

Signals: `sceneReloaded(bundleId)` and `reloadFailed(message)`.

The controller owns its bundle cache — a private directory under
`<CacheLocation>/respin/bundles/<pid>-XXXXXX` — and removes it on destruction. Directories
belonging to processes that are gone are swept on the next start.

---

## Version skew

`respin::Runtime` is a **static** library, so the runtime is baked into your binary at link
time. Upgrading the respin CLI does not upgrade the runtime inside an already-built
application.

A protocol mismatch is reported rather than left silent: the development server sends an
error naming both versions and telling you to rebuild.

A stale `librespin_runtime.a` combined with newer headers is **not** currently detected. If
you upgrade respin, rebuild the application.
