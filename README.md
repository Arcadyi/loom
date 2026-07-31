# loom

Utility-first styling for Qt QML, in the spirit of Tailwind CSS.

Loom gives QML two complementary styling layers on top of one shared set of
design tokens:

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

Both layers resolve from the same token registry, so themes (light/dark or
custom), runtime theme switching, responsive breakpoints, and state variants
(`hover:` `pressed:` `focus:` `disabled:` `dark:`) work identically in both.

## Status

Early development, version 0.1.0. Linux-validated; Qt 6.11, C++20, CMake ≥ 3.22.

## Building

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

## Using in your project

```cmake
find_package(loom CONFIG REQUIRED)
target_link_libraries(YourApp PRIVATE loom::loom loom::loomplugin)
```

Then `import Loom` in QML. No import-path setup is needed: the module registers
itself through the static plugin.

## Documentation

| Document | Contents |
| --- | --- |
| [docs/getting-started.md](docs/getting-started.md) | Install, first styled component |
| [docs/tokens.md](docs/tokens.md) | The typed token scales |
| [docs/utilities.md](docs/utilities.md) | The `Lo.style` utility-string reference |
| [docs/theming.md](docs/theming.md) | Semantic tokens, dark mode, custom themes |
| [docs/responsive.md](docs/responsive.md) | Breakpoint variants |
| [docs/states.md](docs/states.md) | Interaction-state variants |
| [docs/configuration.md](docs/configuration.md) | The JSON config (Tailwind-config equivalent) |
| [docs/limitations.md](docs/limitations.md) | What utilities do on which types, honestly |
| [docs/tooling.md](docs/tooling.md) | `loomstyle`: class checking and completion data |
| [docs/cmake.md](docs/cmake.md) | CMake integration details |

## License

Apache-2.0. See [LICENSE](LICENSE).
