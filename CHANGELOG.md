# Changelog

## 0.2.0

**loom absorbed respin.** The two projects — utility-first styling, and the
Qt/QML build and hot-reload tooling — are now one framework, one package, one
binary. The name `respin` is gone: what was `respin new` is `loom new`,
`respin.json` is `loom.json`, `respin::Application` is `loom::Application`,
`respin_add_application` is `loom_add_application`, and `RESPIN_DEV_*` is
`LOOM_DEV_*`. A project runs `find_package(loom)` once and gets both halves.

### Live design tokens

The capability the merge existed for. `loom dev` already kept C++ services alive
across a QML reload; the token registry lives in that same surviving C++, so a
design file edit now **repaints the running window without recreating the scene
at all**. Nothing on screen loses its state — text stays typed, scroll positions
stay put.

- `loom.json` gains a `design` key naming a token file. `loom dev` watches it,
  `loom_add_application(... DESIGN ...)` compiles it into release builds, and
  `loom style` / `loom lint` load it so project-defined classes resolve.
- `loom::reloadConfig()` replaces rather than merges: tokens reset to the
  built-in set first, so a token deleted from the file stops resolving. A file
  that fails to parse changes nothing — which matters, because a file is
  malformed for most of the time someone is typing in it.
- Attached styles now **recompile**, not just re-apply, when a config changes
  which token names exist. A style string compiled before a token existed had
  already dropped the rule naming it, so re-applying alone re-applied the gap.
- On reload the active theme wins over the file's `defaultTheme`: someone who
  switched to dark to look at it and then saved meant to restyle dark.

### Tooling

- `loomstyle` is folded into the CLI as `loom style --check` / `--catalogue`.
- `loom lint` runs `qmllint` **and** the utility-class check — always both, so a
  `Lo.style` typo is never hidden behind an unrelated qmllint complaint.
- `loom doctor` reports on both halves, including the Loom QML module's
  `qmldir` and `qmltypes`.
- One scaffold template, and it is loom-styled. The `--loom` flag and the
  hand-synced `templates/app-loom/` overlay are gone.

### Packaging

- One `loomTargets` export and one `loomConfig.cmake` replace the two packages,
  exporting `loom::loom`, `loom::loomplugin`, `loom::Runtime` and
  `loom::Protocol`.
- `loom_add_application` defaults `IMPORT_PATHS` to `LOOM_QML_IMPORT_DIR`, so
  `import Loom` resolves for qmllint in a generated project with no wiring.
- New options `LOOM_BUILD_CLI` and `LOOM_BUILD_E2E_TESTS`. `LOOM_BUILD_CLI=OFF`
  still produces a usable styling-only package.
- `MessageType::Design` is additive, so `ProtocolVersion` stays at 1.

---

Everything below shipped while loom was a styling library only.

## Unreleased (pre-merge)

- **Colour opacity modifier**: `bg-surface/70`, `text-foreground/50`,
  `border-outline/25` — Tailwind's trailing `/0`–`/100` on the three colour
  families, composing with every variant. It scales the token's own alpha
  rather than replacing it, and stays attached to the token rather than a
  resolved value, so a theme switch re-resolves and re-applies it. Families
  with no alpha to modify (`w-full/70`) and out-of-range or non-numeric values
  are reported as unknown classes rather than silently dropping the modifier.

- **Recoloured icons**: `Loom.icon(source, color)` returns an image URL that
  serves an asset repainted in a token color, for `icon.source`, `Image.source`
  or anything else taking a URL. It exists because `icon.color` cannot do it —
  Qt tints a control's icon only while the icon item is a mask, which a plain
  file source never is, so the color is accepted and dropped. Recolouring
  happens in an image provider registered on first use, keeps the asset's
  coverage (soft edges stay soft), and overrides the black Qt resolves an
  SVG's `currentColor` to. Point `Loom.iconRoot` (or the config's `iconRoot`)
  at your icon directory once and call sites name assets bare —
  `Loom.icon("home.svg", Loom.color.foreground)`; a source with a scheme or a
  leading `/` bypasses the root. Pass `Loom.color.*` rather than a literal to
  keep the binding live across a theme switch.

- **`loomstyle` tool**: `--check` reports unknown `Lo.style` classes in QML
  literals at build time instead of at runtime (exit 1 on findings, so it drops
  into CTest or CI), and `--dump-catalogue` emits the whole utility vocabulary
  as JSON for editor completion. Both link the library, so they speak exactly
  what the application does; `--config` widens them with project-defined
  tokens. See [docs/tooling.md](docs/tooling.md).
- **Catalogue API**: `<loom/loomcatalogue.h>` exposes `styleCatalogue()`,
  `styleCatalogueJson()` and `unknownStyleClasses()`. The token registry gained
  sorted key enumeration (`colorKeys()`, `spaceKeys()`, …) to back it. The
  catalogue is derived from the parser's own tables, and a test asserts every
  class it emits parses.
- **Box utilities reach Controls**: `bg-*`, `rounded*` and `border*` now route
  to a target's `background` delegate when the target is not itself a Rectangle
  but exposes one, so `Lo.style: "bg-surface rounded-full"` styles a Button
  without hand-writing a `background:` override. Duck-typed on the property
  name and resolved per instance; a background that is not a Rectangle still
  warns as unsupported. The write takes the property over, replacing the
  style's own down/hover colouring — restore it with `hover:`/`pressed:`.
- **Transition utilities**: `transition` / `transition-colors` /
  `transition-opacity` / `transition-all` / `transition-none`, `duration-*`
  and `ease-*` animate Loom's own property writes on theme switches, state
  flips and breakpoint changes; interrupted animations retarget smoothly.
- **Layout-aware margins**: `m-*` now writes the `Layout.*` attached margins
  when the item sits in a RowLayout/ColumnLayout/GridLayout (anchor margins
  otherwise), re-routing on reparent.

## 0.1.0 — 2026-07-30

First release. Utility-first styling for Qt QML with two layers over one
token registry:

- **Typed tokens** — the `Loom` singleton: full Tailwind color palette,
  semantic themable colors, spacing, typography, radius, shadows, opacity,
  durations, easing and breakpoint scales; autocompleted and qmllint-visible
  through generated qmltypes.
- **Utility strings** — the `Lo.style` attached property: `bg-* text-*
  font-* p-* m-* gap-* w-*/h-*/size-* rounded* border* opacity-*
  visible/invisible shadow-*` with `sm:/md:/lg:/xl:` responsive variants
  (window-width, mobile-first) and `hover:/pressed:/focus:/disabled:/dark:`
  state variants. Compiled once per unique string, applied with
  save/restore semantics and value diffing.
- **Theming** — built-in light/dark, runtime switching re-resolves both
  layers live.
- **Configuration** — JSON config for custom colors, spacing, breakpoint
  thresholds and themes (with inheritance); custom tokens usable from
  utility strings.
- **Packaging** — static QML module + plugin, `find_package(loom)` from
  build or install tree, zero runtime import setup; qmldir/qmltypes
  installed for consumer tooling.
- Gallery example app, unit + e2e test suites, CI with sanitizers and
  qmllint gate. Linux-validated; Qt 6.11, C++20.
