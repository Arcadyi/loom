# loom

A UI framework for Qt QML: utility-first styling in the spirit of Tailwind CSS,
plus the build and hot-reload tooling to work with it.

```sh
loom new hello && cd hello && loom dev
```

## Styling

Two complementary layers over one shared set of design tokens:

```qml
import QtQuick
import Loom

// Typed tokens: autocompleted, qmllint-checked, zero magic.
Rectangle {
    color: Loom.color.blue500
    radius: Loom.radius.lg
}

// Utility strings: terse, Tailwind-style, reactive.
Rectangle {
    Lo.style: "p-4 bg-surface rounded-lg hover:bg-blue-600 md:p-6"
}
```

Both resolve from the same token registry, so system/custom themes, runtime
theme switching, viewport and container queries, and the full state/group
variant set work identically in both.

## Tooling

`loom` scaffolds, builds, lints, tests, deploys — and runs a development loop
where **two different things reload**:

- Editing QML rebuilds the scene. C++ services stay alive, and declared state is
  captured and restored.
- Editing your design tokens repaints the running window *without recreating the
  scene at all*. Nothing on screen loses its state.

```console
$ loom dev
[loom] reload server listening on 127.0.0.1:41337
[loom] design tokens changed: design/tokens.json
```

`loom lint` runs `qmllint` **and** checks every `Lo.style` string — a typo in a
utility class is invisible to the compiler and to qmllint, and is exactly the
kind of thing that otherwise shows up as "why is nothing styled":

```console
$ loom lint
qml/Main.qml:44: unknown utility class 'bg-brnad-500'
loom: 1 unknown class(es) in 1 file(s)
```

`loom lsp` wraps Qt's `qmlls` and adds context-aware utility completion,
diagnostics and fixes, resolved-value hovers, and color previews inside those
same strings while preserving ordinary QML language features:

```sh
loom lsp -- --build-dir .loom/build/desktop-debug
```

Configure an LSP-capable editor to launch that command instead of `qmlls`.
For CLion and other IDEs that require the executable itself to be named
`qmlls`, Loom also installs a compatibility shim under
`<prefix>/<libexec>/loom/qmlls`; select it in the IDE's QML language-server
settings.
See [the editor setup reference](docs/tooling/cli.md#loom-lsp).

The same project commands target desktop, Android, iOS, and embedded Linux.
Hosted jobs cover Linux/macOS/Windows, Android emulators, and iOS simulators;
desktop releases produce native DEB, DMG, and MSI packages.

## Status

Version 0.4.0, early development. Qt 6.11, C++20, CMake ≥ 3.22.

The styling half and the tooling half were separate projects (`loom` and
`respin`) through 0.1.0 and merged for 0.2.0.

## Building

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build -LE e2e   # fast tier; -L e2e scaffolds and compiles
cmake --install build --prefix ~/loom-prefix
```

## Using in your project

Styling only:

```cmake
find_package(loom CONFIG REQUIRED)
target_link_libraries(YourApp PRIVATE loom::loom loom::loomplugin)
```

Then `import Loom` in QML. No import-path setup is needed: the module registers
itself through the static plugin.

For hot reload as well, use `loom_add_application` — see
[docs/tooling/cmake.md](docs/tooling/cmake.md), or just start from `loom new`.

## Documentation

Start at [docs/getting-started.md](docs/getting-started.md); the full index is
[docs/README.md](docs/README.md).

| Section | Contents |
| --- | --- |
| [docs/styling/](docs/styling/) | Tokens, the `Lo.style` reference, theming, responsive and state variants, the design token file, the cookbook, and what utilities do on which types |
| [docs/tooling/](docs/tooling/) | The `loom` command, `loom.json`, CMake integration, platform support, troubleshooting |
| [docs/reference/](docs/reference/) | Architecture, the C++ API, the reload wire format, upgrading |

## License

Apache-2.0. See [LICENSE](LICENSE).
