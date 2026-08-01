# loom documentation

New here? [getting-started.md](getting-started.md) installs loom, scaffolds a
project, and styles a first component. Everything below assumes you have done
that once.

The documentation is in three sections. **Styling** is the framework you write
UI against. **Tooling** is the `loom` command and the build integration around
it. **Reference** is what you need when embedding loom, debugging the reload
loop, or upgrading.

## Styling

| Document | Contents |
| --- | --- |
| [styling/utilities.md](styling/utilities.md) | The complete `Lo.style` reference: every class, the property it writes, and the value it resolves to |
| [styling/tokens.md](styling/tokens.md) | The typed `Loom.*` API and every scale, with values |
| [styling/theming.md](styling/theming.md) | Semantic tokens, system/custom themes, motion and contrast |
| [styling/responsive.md](styling/responsive.md) | Viewport and component container queries |
| [styling/states.md](styling/states.md) | Interaction, control, environment, structure, negation, and group variants |
| [styling/configuration.md](styling/configuration.md) | The design token file: custom colours, scales and themes |
| [styling/cookbook.md](styling/cookbook.md) | Complete components — buttons, cards, forms, responsive layouts, Quick Controls |
| [styling/performance.md](styling/performance.md) | What a styled item actually costs, and how to keep it cheap |
| [styling/limitations.md](styling/limitations.md) | What utilities do on which types, and where Qt Quick will not follow CSS |

## Tooling

| Document | Contents |
| --- | --- |
| [tooling/cli.md](tooling/cli.md) | Every `loom` subcommand and flag |
| [tooling/manifest.md](tooling/manifest.md) | `loom.json` |
| [tooling/cmake.md](tooling/cmake.md) | Adding loom to a CMake project |
| [tooling/cmake-api.md](tooling/cmake-api.md) | `loom_add_application`, `loom_enable_hot_reload`, `loom_install_application` |
| [tooling/platforms.md](tooling/platforms.md) | What is supported today, and deployment |
| [tooling/troubleshooting.md](tooling/troubleshooting.md) | When something does not build, run, or reload |

## Reference

| Document | Contents |
| --- | --- |
| [reference/architecture.md](reference/architecture.md) | How styling, the dev server and the runtime fit together |
| [reference/cpp-api.md](reference/cpp-api.md) | The public C++ headers |
| [reference/runtime-api.md](reference/runtime-api.md) | `loom::Application`, embedding, and scene state across a reload |
| [reference/protocol.md](reference/protocol.md) | The reload wire format |
| [reference/upgrading.md](reference/upgrading.md) | Moving between loom versions |

## Finding things

- **"What does this class do?"** — [styling/utilities.md](styling/utilities.md)
  has every class in one table. `loom style --catalogue` emits the same
  vocabulary as JSON, generated from the parser itself.
- **"Why is nothing styled?"** — run `loom lint`. A typo in a utility class is
  invisible to the compiler and to qmllint, and is skipped at run time with a
  warning on the `loom.style` logging category.
- **"Why did my binding stop working?"** — see
  [Property writes versus bindings](styling/limitations.md#property-writes-vs-bindings).
- **"Why did nothing reload?"** —
  [tooling/troubleshooting.md](tooling/troubleshooting.md).

## Conventions in these documents

Utility classes and QML property names are written as `bg-surface` and
`font.pixelSize`. Where a document states a number — a spacing step, a
breakpoint threshold, a timeout — that number is the one compiled into loom,
not an illustration. Where behaviour has a sharp edge, it is stated in the
document that covers the feature rather than collected into a caveats appendix;
[styling/limitations.md](styling/limitations.md) exists for the edges that
belong to no single feature.
