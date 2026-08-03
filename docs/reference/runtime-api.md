# Runtime API

`loom::Runtime` is a static library installed with its headers. A generated `src/main.cpp`
is the whole of the API most applications need:

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

`LOOM_APP_URI` and `LOOM_APP_ENTRY` are defined by `loom_enable_hot_reload`, so the
module URI has one definition rather than being repeated in C++.

---

## `loom::Application`

Owns a `QGuiApplication` and a `QQmlApplicationEngine`, loads the entry scene, and
optionally connects to a development server.

### `void enableDevelopmentRuntime(bool enabled = true)`

Allows the runtime to connect to a development server. Without it the application never
opens a socket, whatever the environment says.

The generated `main.cpp` gates this on `#ifndef NDEBUG`, so `Release`, `RelWithDebInfo` and
`MinSizeRel` builds have no reload path compiled in at all. `loom dev` warns when you
select such a configuration.

### `void setEngineInitializer(EngineInitializer initializer)`

`using EngineInitializer = std::function<void(QQmlApplicationEngine &)>;`

Called with the engine before the scene is loaded, and the place to register C++ types,
context properties, singletons and import paths:

```cpp
loom::Application application(argc, argv);
application.setEngineInitializer([](QQmlApplicationEngine &engine) {
    qmlRegisterType<Backend>("com.example.MyApp", 1, 0, "Backend");
    engine.rootContext()->setContextProperty("buildStamp", QStringLiteral(__DATE__));
});
```

C++ types registered here survive hot reload: only the QML scene is rebuilt, and the engine,
its registrations and every C++ object outlive it. Changing the C++ itself requires a
rebuild, which `loom dev` performs automatically when anything under `src/` changes.

### `int run(const QString &moduleUri, const QString &entryType)`

Loads `entryType` from `moduleUri`, connects the development runtime if enabled, and runs the
event loop. Returns the event loop's exit code, or `1` if the scene could not be loaded — in
which case the QML errors are printed.

### `QGuiApplication &guiApplication()` / `QQmlApplicationEngine &engine()`

Direct access, for anything the wrapper does not cover.

---

## How much of the scene a reload rebuilds

A change is applied to the smallest part of the scene that can hold it. Every `Loader` is a
seam: the Loader itself survives, so the document around it keeps its bindings, and only what
was inside is built again. When every file that changed is behind one, the reload stops there
— the window, its geometry and everything outside the seam are never touched, and the engine
keeps what it has already compiled.

```qml
ApplicationWindow {
    // Editing HomePage.qml, or anything HomePage.qml is built from, replaces
    // what is inside this Loader and nothing else.
    Loader { anchors.fill: parent; source: "pages/HomePage.qml" }
}
```

The scene is rebuilt whole when the change cannot be confined: a file used outside any
`Loader`, one that nothing has instantiated yet, a `Loader` driven by `sourceComponent`
rather than a URL, or a file appearing or disappearing from the project. Editing the window
itself is the ordinary case of the first.

### Keeping the window across every edit

A scene rooted at a `Window` **is** that window, so the one reload it cannot survive is a
change to its own document. Root the scene at an `Item` instead and `loom::Application` puts
it in a window of its own, which belongs to the process rather than to the document and so
outlives every reload — the scene is rebuilt inside a window that keeps its position, size
and focus.

```qml
// Main.qml — no Window, so the window is loom's and survives editing this file
Item {
    Loader { anchors.fill: parent; source: "pages/HomePage.qml" }
}
```

The cost is the window itself: an `Item` root cannot be an `ApplicationWindow`, so there is
no `menuBar`, `header`, `footer` or window-level Controls styling to declare. The scaffolded
application keeps its `ApplicationWindow` for that reason — with a seam in place, the window
already survives everything except edits to the shell. Take the `Item` root when reloading
the shell without losing the window matters more than authoring the window.

This is why a seam is a `Loader` and not any component boundary. An inline `HomePage { }` is
created by the document around it: its `id` lives in that document's context and that
document's bindings point at that instance, so replacing it would leave both dangling. There
is no public way to rebind them, which is the reason a seam has to be declared rather than
inferred. For the same reason, do not bind onto a loaded item from outside — the binding would
address the instance that gets replaced. Give the loaded document its own state instead, as
the scaffolded `HomePage.qml` does with its counter, or put genuinely shared state in
[`Store`](#store-state-that-outlives-the-scene).

### `Store`: state that outlives the scene

State that several documents share has nowhere good to live under the seam rule — it cannot
be bound across the seam, and pushing it down into one page does not make it shared. `Store`
is that place:

```qml
import Loom

Store.route = "settings"

Text { text: Store.route ?? "home" }
```

It is a `QQmlPropertyMap`, so ordinary bindings track individual keys. Its contents live in a
process-wide C++ registry rather than in the QML singleton, which is what makes them survive:
a full reload calls `QQmlEngine::clearSingletons()`, so anything held by a QML singleton dies
with the scene. The singleton you see is a facade, rebuilt and reseeded from the registry
after every reload — the same split `Loom` has over its token registry, which is why design
tokens already survive reloading.

Two consequences worth knowing:

- **It does not participate in scene state capture, and does not need to.** `loomSaveState`
  and the automatic capture exist to carry values across a teardown; `Store` is never torn
  down. It also outlives a *failed* reload and a rollback.
- **Only JSON-representable values are accepted.** A write of a `QObject*` or any other
  pointer is refused with a warning on the `loom.store` category. It would otherwise outlive
  the scene it points into and dangle on the next reload, which is exactly what this is for.
  (Note that `QJsonValue::fromVariant` maps a `QObject*` to *null* rather than to undefined,
  so a naive JSON check would accept one.)

### A seam Loader's `source` must not be a binding

Taking the seam means repointing the Loader, and the runtime does that with a plain property
write. That **destroys any binding on `source`**, permanently — the same rule as
[a property Loom styles](../styling/limitations.md#property-writes-vs-bindings), for the same
reason. A router written the obvious way therefore navigates correctly until the first seam
reload and then silently stops for the rest of the session:

```qml
// Wrong: works until the first seam reload, then never again.
Loader { source: pages[currentPage] }

// Right: assign it.
Loader { id: pageLoader }
onCurrentPageChanged: pageLoader.source = pages[currentPage] ?? ""
Component.onCompleted: pageLoader.source = pages[currentPage] ?? ""
```

This is only a hazard for Loaders that are seams — that is, ones whose `source` is a URL and
whose contents the reload controller may rebuild. A Loader you never edit behind is unaffected,
but there is no cost to assigning either way.

## Scene state across a reload

Whatever a reload rebuilds — the scene or one seam — is destroyed and created again, so the
same capture applies to both. Most state carries across on its own:
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
function loomSaveState() {
    return { "route": currentRoute, "query": search.text }
}

function loomRestoreState(state) {
    currentRoute = state.route ?? "home"
    search.text = state.query ?? ""
}
```

Both are optional; a missing hook now means automatic capture alone rather than a clean
scene. `loomRestoreState` is looked up on the whole type hierarchy, so inheriting it from a
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

`loomRestoreState` is called **synchronously**, immediately after the new root is created
and before the first frame. A queued call would show one frame of un-restored state.

---

## What "last known good" means

If the incoming scene fails to construct, the runtime reloads the previous bundle, or the
compiled-in resources if there is no previous bundle.

"Last known good" means **constructed successfully** — not "runs without warnings". A bundle
that compiles and builds its object tree is accepted even if it then logs QML errors at run
time. Those errors are reported over the wire and appear in the `loom dev` terminal, but
they do not trigger a rollback: a warning means the scene is up and something in it
misbehaved, and rolling back on that would undo working reloads.

A bundle that does not **compile** never reaches this point. It is rejected before anything
is torn down, so the running scene is untouched — no teardown, no cleared caches, no
rollback. This is the common case: it is what happens every time a QML file is saved
mid-edit.

---

## `loom::ReloadController`

The engine bootstrap and reload machinery behind `Application`. Use it directly only when
you are not using `loom::Application` — for instance when embedding loom in an existing
application that owns its own engine.

```cpp
loom::ReloadController controller(engine);
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
`<CacheLocation>/loom/bundles/<pid>-XXXXXX` — and removes it on destruction. Directories
belonging to processes that are gone are swept on the next start.

---

## Version skew

`loom::Runtime` is a **static** library, so the runtime is baked into your binary at link
time. Upgrading the loom CLI does not upgrade the runtime inside an already-built
application.

A protocol mismatch is reported rather than left silent: the development server sends an
error naming both versions and telling you to rebuild.

A stale `libloom_runtime.a` combined with newer headers is **not** currently detected. If
you upgrade loom, rebuild the application.
