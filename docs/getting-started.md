# Getting started

## Requirements

- Qt 6.11 or newer (Quick, Qml, Network; QmlPrivate for the runtime)
- CMake 3.22 or newer, Ninja, C++20 compiler
- Linux is the validated platform

## Install

```sh
git clone https://github.com/Arcadyi/loom && cd loom
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix ~/loom-prefix
export PATH="$HOME/loom-prefix/bin:$PATH"
```

Confirm the toolchain and both halves of the package:

```console
$ loom doctor
loom doctor — desktop
[ok]      C++ compiler: /usr/bin/c++
[ok]      CMake: /usr/bin/cmake
[ok]      Ninja: /usr/bin/ninja
[ok]      Qt qmake: /usr/bin/qmake6 (Qt 6.11.1)
[ok]      loom CMake package: …/lib/cmake/loom/loomConfig.cmake
[ok]      Loom QML module: …/lib/qml/Loom
```

## A new application

```sh
loom new hello --org com.example
cd hello
loom dev
```

`loom new` scaffolds a project that is already styled with Loom, with the
manifest, a design token file, and CI-ready CMake:

```
hello/
├── loom.json            manifest: applications, and the design file below
├── design/tokens.json   your palette, spacing, breakpoints and themes
├── qml/Main.qml         import Loom, styled with Lo.style
├── src/main.cpp         loom::Application, ~10 lines
└── tests/tst_smoke.cpp
```

`loom dev` builds it, runs it, and watches. Two things reload, differently:

- **Editing QML** rebuilds the scene. C++ services stay alive, and declared
  state is captured and restored.
- **Editing `design/tokens.json`** repaints the running window *without*
  recreating the scene at all. Nothing on screen loses its state — text stays
  typed, scroll positions stay put.

Try it: with `loom dev` running, change a colour in `design/tokens.json` and
save.

## Adding Loom to an existing project

`loom init` writes a manifest for a project that already has its own CMake:

```sh
cd existing-project
loom init --apply
```

Or wire it by hand:

```cmake
find_package(loom CONFIG REQUIRED)
target_link_libraries(YourApp PRIVATE loom::loom loom::loomplugin)
```

Configure with `-DCMAKE_PREFIX_PATH=<prefix-or-loom-build-dir>`. Both the
install tree and the build tree export the package, so
`-DCMAKE_PREFIX_PATH=~/src/loom/build` works without installing.

Linking `loom::loomplugin` registers the QML module through Qt's static-plugin
machinery: no import paths, no `qml.conf`, nothing at runtime.

For hot reload as well, use `loom_add_application` instead — see
[cmake.md](cmake.md).

## First styled component

```qml
import QtQuick
import Loom

Rectangle {
    // Typed tokens: autocompleted, checked by qmllint.
    color: Loom.color.surface
    radius: Loom.radius.lg

    Text {
        anchors.centerIn: parent
        // Utility strings: terse, variant-aware.
        Lo.style: "text-foreground text-lg font-semibold"
        text: "Hello, Loom"
    }
}
```

Both layers resolve from the same registry: switching `Loom.theme`, or saving
the design file under `loom dev`, re-resolves every binding and every applied
utility, live.

`Lo.style` is a string, so a typo in it is invisible to the compiler and to
qmllint. `loom lint` checks both:

```console
$ loom lint
qml/Main.qml:44: unknown utility class 'bg-brnad-500'
loom: 1 unknown class(es) in 1 file(s)
```

## Where to next

**Styling**

- [tokens.md](tokens.md) — every typed scale
- [utilities.md](utilities.md) — the full `Lo.style` grammar
- [theming.md](theming.md) — semantic tokens and dark mode
- [responsive.md](responsive.md) — breakpoint variants
- [states.md](states.md) — hover, pressed, focus, disabled
- [configuration.md](configuration.md) — the design token file
- [limitations.md](limitations.md) — what utilities do on which types

**Building and running**

- [tooling.md](tooling.md) — the `loom` command reference
- [manifest.md](manifest.md) — `loom.json`
- [cmake.md](cmake.md) — CMake integration and `loom_add_application`
- [runtime-api.md](runtime-api.md) — `loom::Application` and embedding
- [architecture.md](architecture.md) — how the dev server and reload work
- [troubleshooting.md](troubleshooting.md) — when something does not reload
