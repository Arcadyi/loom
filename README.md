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

Both resolve from the same token registry, so themes (light/dark or custom),
runtime theme switching, responsive breakpoints, and state variants
(`hover:` `pressed:` `focus:` `disabled:` `dark:`) work identically in both.

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

## Status

Version 0.2.1, early development. Linux-validated; Qt 6.11, C++20, CMake ≥ 3.22.

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
[docs/cmake.md](docs/cmake.md), or just start from `loom new`.

## Documentation

**Styling**

| Document | Contents |
| --- | --- |
| [docs/getting-started.md](docs/getting-started.md) | Install, first project, first styled component |
| [docs/tokens.md](docs/tokens.md) | The typed token scales |
| [docs/utilities.md](docs/utilities.md) | The `Lo.style` utility-string reference |
| [docs/theming.md](docs/theming.md) | Semantic tokens, dark mode, custom themes |
| [docs/responsive.md](docs/responsive.md) | Breakpoint variants |
| [docs/states.md](docs/states.md) | Interaction-state variants |
| [docs/configuration.md](docs/configuration.md) | The design token file |
| [docs/limitations.md](docs/limitations.md) | What utilities do on which types, honestly |

**Building and running**

| Document | Contents |
| --- | --- |
| [docs/tooling.md](docs/tooling.md) | The `loom` command reference |
| [docs/manifest.md](docs/manifest.md) | `loom.json` |
| [docs/cmake.md](docs/cmake.md) | CMake integration |
| [docs/cmake-api.md](docs/cmake-api.md) | `loom_add_application` and friends |
| [docs/runtime-api.md](docs/runtime-api.md) | `loom::Application`, embedding, state hooks |
| [docs/architecture.md](docs/architecture.md) | How the dev server and reload work |
| [docs/protocol.md](docs/protocol.md) | The reload wire format |
| [docs/platforms.md](docs/platforms.md) | Platform support, honestly |
| [docs/troubleshooting.md](docs/troubleshooting.md) | When something does not reload |

## License

Apache-2.0. See [LICENSE](LICENSE).
