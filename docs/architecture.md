# Architecture

## Components

`loom` is deliberately split so production applications do not depend on the
project-management CLI.

- `loom::loom` is the styling library: the token registry, the typed `Loom`
  singleton and the `Lo.style` compiler. Usable entirely on its own.
- `loom` (the `loom_cli` target, installed as `bin/loom`) is a QCoreApplication
  host executable. It owns manifests, project generation, toolchain
  diagnostics, builds, file watching, and development connections. It links the
  styling library for the offline class checker behind `loom style`, but never
  creates a QML engine.
- `loom::Protocol` contains the versioned framed transport and bundle validation.
- `loom::Runtime` owns the QML engine bootstrap and development reload
  controller. It links `loom::loom` publicly, which is what lets design tokens
  be reloaded in the running process.
- `loomFunctions.cmake` provides the starter application wrapper and an
  integration function for conventional Qt targets.

## Reload lifecycle

1. The CLI configures and builds a Debug application.
2. It binds a loopback server, creates a random 256-bit token, and launches the
   application with host, port, and token environment variables.
3. The runtime loads the embedded resource scene immediately, then connects
   outbound and authenticates.
4. The CLI maps QML roots beneath `qt/qml/<module URI>/` and assets beneath the
   module's `assets/` directory. It hashes and transfers the complete bundle.
5. The runtime validates paths and hashes, writes into a staging directory,
   marks it `.loom-complete`, and renames it into the active bundle cache.
   The cache root is private to the process (`<cache>/loom/bundles/<pid>-XXXXXX`),
   and a directory without the completion marker is never loaded.
6. It compiles the incoming scene *before* touching the running one. A bundle
   that does not compile is rejected with the live scene untouched -- no
   teardown, no cleared caches, no rollback.
7. Once the candidate compiles, it invokes `loomSaveState`, destroys the old
   root, clears QML caches, creates the replacement root, and calls
   `loomRestoreState` **synchronously**. A queued restore would run after the
   first frame and show one frame of un-restored state.
8. If construction still fails, the runtime clears caches again and reloads the
   previous bundle or compiled resource. "Last known good" means "constructed
   successfully": QML warnings are reported over the wire but never trigger a
   rollback, because a warning means the scene is up and something in it
   misbehaved.

Complete bundles make file deletion and rapid editor writes deterministic. A
120 ms debounce coalesces common save sequences.

## Design token reload

Design tokens do **not** go through the bundle path. The token registry is a
process-wide singleton in C++ that outlives every scene reload, so a change to
the manifest's `design` file can be applied to the running process directly:

1. The CLI watches the design file on its own watcher, with the same 120 ms
   debounce, and sends its raw bytes as `MessageType::Design`. It also sends it
   on connect, because the copy compiled into the application is only as fresh
   as the last build.
2. The runtime validates the size and parses the JSON *before* touching
   anything, then applies it with `loom::reloadConfig`, which resets every token
   to the built-in set before merging the file in. Without the reset, a token
   deleted from the file would survive the reload that removed it.
3. The registry emits `vocabularyChanged` and then `tokensChanged`. Attached
   styles recompile on the first and re-apply on the second -- a compiled style
   string dropped any rule naming a token that did not exist when it was
   compiled, so re-applying alone would faithfully re-apply that gap.

The scene is never rebuilt, so nothing on screen loses state: text stays typed,
scroll positions stay put. A malformed file changes nothing and leaves the
previous tokens live, which matters because a file is malformed for most of the
time someone is typing in it.

`MessageType::Design` is additive, so `ProtocolVersion` stays at 1: a runtime
built before it existed rejects the type as unknown and keeps running.

Changes beneath `src/` or `cmake/`, plus the root `CMakeLists.txt` and
`loom.json`, use a separate 300 ms watcher. The CLI terminates the child,
reconfigures incrementally, rebuilds, and restarts it. A failed native rebuild
leaves the development server running so the next edit can recover; a rebuild
that succeeds but produces an application that cannot start ends the session
with exit 127 rather than waiting forever.

## Build tree layout

`loom_add_application` puts application and test binaries in
`<build>/bin/`. This is a contract, not an implementation detail: `loom dev`
resolves the executable from exactly that path. It exists because a target whose
name matches a directory the build tree creates -- `Testing`, which `ctest`
creates on every run -- cannot be linked at the top level.

Projects integrated through `loom init` keep whatever layout they had;
`loom_enable_hot_reload` warns when a target name collides with a reserved
build-tree directory, since it cannot rewrite that project's CMake.

## Production boundary

Production source and assets are still supplied to `qt_add_qml_module`, so QML
cache generation, lint targets, resource embedding, deployment scanning, and
normal Qt tooling continue to work. The generated main only enables the
development connection in builds where `NDEBUG` is not defined.

The runtime accepts connections only when the application explicitly enables
development mode and receives all three connection variables.
