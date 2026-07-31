# Getting started

## Requirements

- Qt 6.11 or newer (Quick and Qml modules)
- CMake 3.22 or newer, C++20 compiler
- Linux is the validated platform

## Install

```sh
git clone <loom repo> && cd loom
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix ~/loom-prefix   # or use the build tree directly
```

## Wire into your project

```cmake
find_package(loom CONFIG REQUIRED)
target_link_libraries(YourApp PRIVATE loom::loom loom::loomplugin)
```

Configure with `-DCMAKE_PREFIX_PATH=<prefix-or-loom-build-dir>`. Both the
install tree and the build tree export the package, so
`-DCMAKE_PREFIX_PATH=~/src/loom/build` works without installing.

Linking `loom::loomplugin` registers the QML module through Qt's static-plugin
machinery: no import paths, no `qml.conf`, nothing at runtime.

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

Both layers resolve from the same registry: switching `Loom.theme` re-resolves
every binding and every applied utility, live.

## Where to next

- [tokens.md](tokens.md) — every typed scale
- [utilities.md](utilities.md) — the full `Lo.style` grammar
- [theming.md](theming.md) — semantic tokens and dark mode
- [configuration.md](configuration.md) — custom palettes, spacing and themes
- [limitations.md](limitations.md) — what utilities do on which types
