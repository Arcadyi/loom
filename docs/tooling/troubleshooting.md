# Troubleshooting

Failures actually hit while developing loom, and what each one means.

Start with `loom doctor`, which checks the C++ compiler, CMake, Ninja, CTest, Qt 6.11 and
whether loom can find its own CMake package. `loom doctor --json` if you want to script it.

---

## `cannot open output file <Name>: Is a directory`

A link failure where the target name matches a directory the build tree creates. `ctest`
creates `Testing/` on every run, so an application called `Testing` collides with it.

`loom_add_application` avoids this by putting binaries in `<build>/bin/`. If you reached
this through `loom init`, loom cannot rewrite your project's output paths for you — it
warns at configure time, and the fix is to set `RUNTIME_OUTPUT_DIRECTORY` on the target or
rename it.

---

## `Could not find a package configuration file provided by "loom"`

The generated project does `find_package(loom CONFIG REQUIRED)` and CMake cannot find it.

`loom build`, `test`, `dev` and `deploy` locate their own package relative to the running
binary, so this normally means loom is being run from a build tree rather than an install
prefix. Either install it, or pass `--prefix <prefix>`.

`loom doctor` reports this directly as **loom CMake package**.

---

## The application builds and runs but never hot-reloads

Check the configuration. `Release`, `RelWithDebInfo` and `MinSizeRel` all define `NDEBUG`,
and the generated `src/main.cpp` gates `enableDevelopmentRuntime()` on `#ifndef NDEBUG`, so
there is no reload path compiled in at all. Such a build launches, the server serves bundles,
and nothing happens.

`loom dev --config Release` warns about this up front. Use `Debug` for development.

---

## A singleton is not a singleton

Referring to a type by name (`Theme.accent`) only works for a registered singleton. Adding
`pragma Singleton` to the QML file is not enough on its own — the module has to declare it:

```cmake
loom_add_application(MyApp
    ...
    SINGLETONS Theme.qml
)
```

Without that, the type loads as an ordinary component and the reference fails.

---

## A reload does nothing, silently

If edits stop taking effect while `loom dev` still prints `reloaded bundle …`, the engine
is loading the compiled-in copy rather than the bundled one. The cause is a `prefer`
directive in the module's qmldir; loom strips it when bundling, so this should not happen —
if it does, it is a bug worth reporting.

If `loom dev` prints nothing at all on save, the file is probably not under a declared
`qmlRoots` directory. Check `loom.json`.

---

## `loom dev` says "application connected" but reloads do nothing

This was possible before the protocol had a heartbeat: a half-open connection is
indistinguishable from an idle one at the TCP level. The server now pings every 5 seconds and
drops a client silent for 20; the runtime reconnects after 30 seconds of server silence.

If you see it now, look for the runtime's own message:

```
loom: no word from the development server for 30 seconds; reconnecting
```

---

## The application exits immediately with no output

Fixed: a scene that fails to load now prints its QML errors. If you are on an older build,
the symptom is exit code 1 and complete silence; rebuild against a current loom to see the
error.

---

## Warnings from the application do not appear

Qt sends `qWarning` to journald when stderr is not a terminal. Under `loom dev` output is
forwarded to your terminal and you see it, but a scripted or redirected run will not.

```sh
QT_FORCE_STDERR_LOGGING=1 loom dev > dev.log 2>&1
```

---

## `CMake Error: ... generator ... does not match the generator used previously`

A build tree was configured with a different generator. loom passes `-G` only when the
build tree has no cache yet, precisely so this does not happen — but a tree configured before
that, or by hand, will still hit it.

```sh
loom clean          # this configuration
loom clean --all    # every configuration
```

---

## `loom build` refuses with "this project defines N applications"

A multi-application project with no default. Pass `--app <target>`, or set
`project.defaultApplication` in `loom.json`. See [manifest.md](../tooling/manifest.md).

---

## `unknown configuration '...'`

`--config` takes `Debug`, `Release`, `RelWithDebInfo` or `MinSizeRel`, case-insensitively.
`debug` and `Debug` resolve to the same build directory and the same cache value, so
alternating between them does not force a rebuild.

---

## The deployed application does not run on another machine

`loom deploy` does not bundle Qt; the target host needs Qt 6.11. See
[platforms.md](../tooling/platforms.md#why-qt-is-not-bundled) for why, and use `linuxdeploy` or
`appimage-builder` over the installed prefix for a self-contained artifact.

---

## Protocol version mismatch

```
protocol version mismatch: this loom speaks v1, the application was built
against v0. Rebuild the application against the installed loom.
```

`loom::Runtime` is a static library, so upgrading the CLI does not upgrade the runtime
inside an already-built application. Rebuild it.

Note that a stale `libloom_runtime.a` combined with newer headers is not detected — if you
upgrade loom, rebuild rather than relinking.
