# Changelog

## Unreleased

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
