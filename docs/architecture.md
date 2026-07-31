# Architecture

## Components

`respin` is deliberately split so production applications do not depend on the
project-management CLI.

- `respin` is a QtCore/QtNetwork host executable. It owns manifests, project
  generation, toolchain diagnostics, builds, file watching, and development
  connections.
- `respin::Protocol` contains the versioned framed transport and bundle validation.
- `respin::Runtime` owns the QML engine bootstrap and development reload controller.
- `respinFunctions.cmake` provides the starter application wrapper and an
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
   marks it `.respin-complete`, and renames it into the active bundle cache.
   The cache root is private to the process (`<cache>/respin/bundles/<pid>-XXXXXX`),
   and a directory without the completion marker is never loaded.
6. It compiles the incoming scene *before* touching the running one. A bundle
   that does not compile is rejected with the live scene untouched -- no
   teardown, no cleared caches, no rollback.
7. Once the candidate compiles, it invokes `respinSaveState`, destroys the old
   root, clears QML caches, creates the replacement root, and calls
   `respinRestoreState` **synchronously**. A queued restore would run after the
   first frame and show one frame of un-restored state.
8. If construction still fails, the runtime clears caches again and reloads the
   previous bundle or compiled resource. "Last known good" means "constructed
   successfully": QML warnings are reported over the wire but never trigger a
   rollback, because a warning means the scene is up and something in it
   misbehaved.

Complete bundles make file deletion and rapid editor writes deterministic. A
120 ms debounce coalesces common save sequences.

Changes beneath `src/` or `cmake/`, plus the root `CMakeLists.txt` and
`respin.json`, use a separate 300 ms watcher. The CLI terminates the child,
reconfigures incrementally, rebuilds, and restarts it. A failed native rebuild
leaves the development server running so the next edit can recover; a rebuild
that succeeds but produces an application that cannot start ends the session
with exit 127 rather than waiting forever.

## Build tree layout

`respin_add_application` puts application and test binaries in
`<build>/bin/`. This is a contract, not an implementation detail: `respin dev`
resolves the executable from exactly that path. It exists because a target whose
name matches a directory the build tree creates -- `Testing`, which `ctest`
creates on every run -- cannot be linked at the top level.

Projects integrated through `respin init` keep whatever layout they had;
`respin_enable_hot_reload` warns when a target name collides with a reserved
build-tree directory, since it cannot rewrite that project's CMake.

## Production boundary

Production source and assets are still supplied to `qt_add_qml_module`, so QML
cache generation, lint targets, resource embedding, deployment scanning, and
normal Qt tooling continue to work. The generated main only enables the
development connection in builds where `NDEBUG` is not defined.

The runtime accepts connections only when the application explicitly enables
development mode and receives all three connection variables.
