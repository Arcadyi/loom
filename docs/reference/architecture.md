# Architecture

How the pieces fit, and why they are shaped the way they are. If you only want
to *use* loom, [../styling/utilities.md](../styling/utilities.md) and
[../tooling/cli.md](../tooling/cli.md) are the documents you want.

## Components

loom is deliberately split so production applications do not depend on the
project-management CLI.

| Target | Contents |
| --- | --- |
| `loom::loom` | the styling library: the token registry, the typed `Loom` singleton, the `Lo.style` compiler and applier. Usable entirely on its own |
| `loom::loomplugin` | the `Loom` QML module's static plugin, so `import Loom` resolves with no import-path setup |
| `loom::Protocol` | the versioned framed transport and bundle validation |
| `loom::Runtime` | the QML engine bootstrap and the reload controller. Links `loom::loom` publicly |
| `loom_cli` → `bin/loom` | a `QCoreApplication` host: manifests, project generation, toolchain diagnostics, builds, file watching, dev connections, and the `qmlls` proxy |
| `loomFunctions.cmake` | the starter-application wrapper and the integration function for conventional Qt targets |

Two boundaries are load-bearing:

- **The CLI never creates a QML engine.** It links the styling library for the
  offline class checker behind `loom style`, which is what lets the checker
  speak exactly the vocabulary the application does, but it stays a console
  application.
- **`loom::Runtime` links `loom::loom` publicly.** That is the structural
  expression of the merge: an application with hot reload also has the styling
  layer, and the reload controller can therefore apply design tokens to the
  running process without going anywhere near the scene.

---

## The styling pipeline

Two layers sit over one registry:

```
                 ┌─────────────────────┐
   Loom.color.*  │                     │  Lo.style: "bg-surface ..."
   (typed QML) ──┤  LoomTokenRegistry  ├── (utility strings)
                 │                     │
                 └─────────────────────┘
```

They never disagree because there is nothing to disagree about — both resolve
names against the same tables. A theme switch, a design reload, a
config-defined colour: all of it lands in one place and both layers see it.

### The registry

A process-wide singleton holding ten scales — colours, space, text sizes, font
weights, tracking, radius, shadows, opacity, durations, easings — plus the
breakpoint thresholds and the theme table.

It emits two distinct signals, and the distinction matters:

- **`tokensChanged`** — some token may resolve to a different *value* now. A
  theme switch does this. Listeners re-apply.
- **`vocabularyChanged`** — the set of token *names* changed. Only a config load
  does this. Listeners must **recompile**, not just re-apply.

The second exists because a compiled style string dropped any rule naming a
token that did not exist when it was compiled. If a design file later defines
`brand-500`, re-applying the old compiled form would faithfully re-apply the
gap. `vocabularyChanged` is emitted first, then `tokensChanged`, so a recompile
is always followed by an apply.

### Editor intelligence

`loom lsp` speaks LSP over stdio and launches the real Qt `qmlls` as a child.
The separately installed `qmlls` compatibility executable enters that same
proxy directly for IDEs such as CLion that select a server by filename. It is
kept outside the normal binary directory so Qt's executable remains distinct.
Messages Loom does not augment pass through unchanged, including Qt's build-dir
extension. The proxy tracks the editor's open-document snapshots so it can find
the exact string segment and replacement range without asking `qmlls` to
understand the contents of a string.

For each document it finds the nearest `loom.json`, activates that project's
design tokens, and reads completion, hover and validation data from the same
catalogue and compiler as the runtime. `qmlls` and Loom diagnostics are cached
separately and republished as one versioned list, because every LSP diagnostic
notification replaces the previous list from that server.

### Compiling a style string

`Lo.style` strings are parsed into a `LoomCompiledStyle` — a flat list of rules,
each carrying a utility, a token *key*, a breakpoint tier, a state mask and a
specificity rank.

**Rules store token names, never resolved values.** That single decision is what
makes a theme switch cheap: nothing recompiles, the same rules simply resolve
differently. It is also why `vocabularyChanged` needs its own signal — names are
resolved late, but *which names parsed at all* was decided early.

Compiled styles are cached process-wide by exact string and shared by
`shared_ptr`, so a thousand items with the same string hold one compiled result.

### Target profiles

Loom writes real QML properties, and which property depends on the type. A
`LoomTargetProfile` maps each utility to a property path for one `QMetaObject`,
built once per type and cached:

- `bg-*` → `color` on a Rectangle;
- `p-*` → `topPadding` and friends on anything declaring them;
- `gap-*` → `spacing` on anything declaring it;
- `m-*` → `anchors.topMargin`, or `Layout.topMargin` inside a Layout.

The lookup is **duck typing on property names**, not a type whitelist. That is
what lets your own component opt into `p-*` by declaring
`property real topPadding`, and what lets every Quick Control work without loom
knowing they exist. A utility with no path on this type warns once per
type-and-utility pair and is skipped.

One profile per `QMetaObject` is cached process-wide, so per-item cost is an
array index.

### Applying

An apply pass is queued to the event loop and coalesced — a theme switch, a
resize and a hover in one turn produce one pass. The first apply is deferred by
a turn on purpose: that puts it *after* the item's own initial property
assignments, so `Lo.style` wins regardless of declaration order in the document.

The pass:

1. reads the current state bits and breakpoint tier;
2. discards rules whose tier or state does not match;
3. resolves survivors' token names against the active theme;
4. ranks by specificity into a hash keyed by property path;
5. releases properties no longer wanted, restoring saved values;
6. writes the rest, skipping any write whose value already matches.

Steps 5 and 6 are the save/restore contract: **the first write to a property
saves its previous value**, and when a class stops applying the saved value is
put back. What cannot be put back is a *binding* — an imperative write destroys
it permanently, which is standard QML semantics rather than something loom adds.
Hence the rule that a property is either yours or Loom's.

### Why specificity has two axes

Breakpoints and states are ranked separately, states winning, then breakpoint
tier, then position in the string.

Ranking them on a single count of variant prefixes — which loom did through
0.2.0 — makes `hover:bg-accent` and `md:bg-red-500` tie, so the later-written
class wins. In practice that silently disabled every `hover:` rule above 768 px.
The two-axis rank encodes the actual relationship: a breakpoint says *where*, a
state says *when*, and a transient state should override the static appearance
at any width.

### Managed items

Two utilities cannot be expressed as property writes and create real items
instead:

- **`hover:` / `pressed:`** need an input source. Loom adds one invisible child
  holding a `HoverHandler` and a passive `TapHandler`, stacked at `z: -1` so it
  never steals events from the target's own handlers. When the target has its
  own `bool pressed` property, that is preferred and no watcher is created.
- **`shadow-*`** creates a `RectangularShadow` child at `z: -1`, bound to the
  target's geometry and corner radii. A child rather than a sibling, so a
  positioner or Layout never lays it out as content of its own.

Both are ordinary scene-graph items and visible to code walking `children`.

---

## Reload lifecycle

1. The CLI configures and builds a Debug application.
2. It binds a loopback server, creates a random 256-bit token, and launches the
   application with host, port and token environment variables.
3. The runtime loads the embedded resource scene immediately, then connects
   outbound and authenticates.
4. The CLI maps QML roots beneath `qt/qml/<module URI>/` and assets beneath the
   module's `assets/` directory. It hashes and transfers the complete bundle.
5. The runtime validates paths and hashes, writes into a staging directory,
   marks it `.loom-complete`, and renames it into the active bundle cache. The
   cache root is private to the process
   (`<cache>/loom/bundles/<pid>-XXXXXX`), and a directory without the completion
   marker is never loaded.
6. It **compiles the incoming scene before touching the running one**. A bundle
   that does not compile is rejected with the live scene untouched — no
   teardown, no cleared caches, no rollback.
7. Once the candidate compiles, it invokes `loomSaveState`, destroys the old
   root, clears QML caches, creates the replacement root, and calls
   `loomRestoreState` **synchronously**. A queued restore would run after the
   first frame and show one frame of un-restored state.
8. If construction still fails, the runtime clears caches again and reloads the
   previous bundle or the compiled-in resource.

"Last known good" means "constructed successfully". QML warnings are reported
over the wire but never trigger a rollback, because a warning means the scene is
up and something in it misbehaved — tearing it down would lose more than it
saves.

Complete-bundle semantics make file deletion and rapid editor writes
deterministic. A 120 ms debounce coalesces common save sequences.

## Design token reload

Design tokens do **not** go through the bundle path. The registry is a
process-wide C++ singleton that outlives every scene reload, so a change to the
design file is applied to the running process directly:

1. The CLI watches the design file on its own watcher, with the same 120 ms
   debounce, and sends the document together with its path in the project as
   `MessageType::Design`. It also sends it on connect, because the copy compiled
   into the application is only as fresh as the last build.
2. The runtime validates size and parses the JSON *before* touching anything,
   then applies it from memory with `loom::reloadConfigData`, which resets every
   token to the built-in set before merging the file in. Without the reset, a
   token deleted from the file would survive the reload that removed it.
3. The registry emits `vocabularyChanged` and then `tokensChanged`. Attached
   styles recompile on the first and re-apply on the second.

The scene is never rebuilt, so nothing on screen loses state: text stays typed,
scroll positions stay put. A malformed file changes nothing and leaves the
previous tokens live, which matters because a file is malformed for most of the
time someone is typing in it.

The path travels with the document because the runtime never writes it to disk
where it belongs. A relative `iconRoot` resolves against that path; resolving it
against the bytes' own location pointed every icon at a directory nothing could
open. Reshaping the frame this way is what took `ProtocolVersion` to 2 — see
[protocol.md](protocol.md).

## Native rebuild

Changes beneath `src/` or `cmake/`, plus the root `CMakeLists.txt` and
`loom.json`, use a separate 300 ms watcher. The CLI terminates the child,
reconfigures incrementally, rebuilds, re-reads `loom.json`, refreshes the
bundle, and restarts the application.

Re-reading the manifest matters because `loom.json` can retarget the QML roots,
the entry point or the design file, and the server would otherwise keep watching
what the session started with. Refreshing the bundle matters because the bundled
`qmldir` comes from the build tree, so a CMake edit that adds a singleton
changes the bundle without touching any watched QML file.

A failed native rebuild leaves the development server running so the next edit
can recover. A rebuild that succeeds but produces an application that cannot
start ends the session with exit 127 rather than waiting forever.

## Build tree layout

`loom_add_application` puts application and test binaries in `<build>/bin/`.
This is a contract, not an implementation detail: `loom dev` resolves the
executable from exactly that path.

It exists because a target whose name matches a directory the build tree creates
— `Testing`, which `ctest` creates on every run — cannot be linked at the top
level. The e2e suite scaffolds an application called `Testing` specifically to
keep that regression covered.

Projects integrated through `loom init` keep whatever layout they had;
`loom_enable_hot_reload` warns when a target name collides with a reserved
build-tree directory, since it cannot rewrite that project's CMake.

## Production boundary

Production source and assets are still supplied to `qt_add_qml_module`, so QML
cache generation, lint targets, resource embedding, deployment scanning and
normal Qt tooling all continue to work.

The generated `main.cpp` enables the development connection only where `NDEBUG`
is undefined, so a Release build has **no reload path compiled in at all** —
not a disabled one. On top of that the runtime opens a socket only when the
application explicitly called `enableDevelopmentRuntime()` *and* all three
connection variables are present, and refuses any host that is not loopback.
