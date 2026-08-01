# Getting started

## Requirements

- Qt 6.11 or newer (Quick, Qml, Network; QmlPrivate for the runtime)
- CMake 3.22 or newer, Ninja, C++20 compiler
- Linux, macOS, and Windows desktop builds are hosted; Android and iOS use
  hosted emulator/simulator jobs

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
├── loom.json                       applications and design manifest
├── design/tokens.json              palette, breakpoints and themes
├── qml/Main.qml                    window and hot-reload state
├── qml/pages/HomePage.qml          responsive starter screen
├── qml/components/FeatureCard.qml  first reusable component
├── src/main.cpp                    native application entry point
└── tests/tst_smoke.cpp             CTest smoke target
```

CLion users can open the directory directly. The scaffold's ignored local IDE
settings enable QML language-server completion and select Loom's `qmlls` proxy;
let the initial CMake reload finish so the server has the generated module
metadata and build directory.

`loom dev` builds it, runs it, and watches. Two things reload, differently:

- **Editing QML** rebuilds the scene. C++ services stay alive, and declared
  state is captured and restored.
- **Editing `design/tokens.json`** repaints the running window *without*
  recreating the scene at all. Nothing on screen loses its state — text stays
  typed, scroll positions stay put.

Try it: with `loom dev` running, change a colour in `design/tokens.json` and
save.

Press Ctrl+Shift+I to open the development inspector. Hover a styled item to
see its utility string, active variants, theme, and resolved property writes;
click to lock the selection.

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
[cmake.md](tooling/cmake.md).

## First styled component

```qml
import QtQuick
import Loom

Rectangle {
    // Typed tokens: autocompleted, checked by qmllint.
    color: Loom.color.surface
    radius: Loom.radius.lg

    Text {
        // Utility strings: terse, variant-aware — and `center` anchors this
        // label in its parent without a line of anchors.
        Lo.style: "text-foreground text-lg font-semibold center"
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

## Adding states

The reason to reach for a utility string rather than typed tokens is variants.
Everything an interaction does to this card is one line:

```qml
import QtQuick
import Loom

Rectangle {
    width: 200
    height: 120
    Lo.style: "bg-surface border border-outline rounded-lg"
             + " hover:bg-surface-alt hover:border-accent"
             + " transition-colors duration-150"
}
```

No `MouseArea`, no `states` block, no `Behavior`. Loom attaches a hover source,
and `transition-colors` animates the writes it makes.

The same string carries responsive and theme variants — `md:rounded-xl`,
`dark:border-slate-700` — and they compose in any order.

## Where to next

The full index is [README.md](README.md). The three that follow on directly
from here:

- **[styling/cookbook.md](styling/cookbook.md)** — complete components: buttons,
  cards, forms, responsive pages, Quick Controls, and migrating a hand-styled
  file.
- **[styling/utilities.md](styling/utilities.md)** — every class, the property
  it writes, and the scales behind every key.
- **[tooling/cli.md](tooling/cli.md)** — every `loom` subcommand and flag.

Then, as you need them:

| Question | Document |
| --- | --- |
| How do I theme this? | [styling/theming.md](styling/theming.md) |
| What are my own colours called? | [styling/configuration.md](styling/configuration.md) |
| Why did this utility do nothing? | [styling/limitations.md](styling/limitations.md) |
| Why does nothing reload? | [tooling/troubleshooting.md](tooling/troubleshooting.md) |
| How do I embed loom in my own engine? | [reference/runtime-api.md](reference/runtime-api.md) |
| What does a styled item cost? | [styling/performance.md](styling/performance.md) |
