# Limitations, stated honestly

Loom's utility layer writes real QML properties on real types; it does not
invent a box model that Qt Quick doesn't have. That draws some hard lines.

## Per-type support

| Utility | Works on | Elsewhere |
| --- | --- | --- |
| `bg-*`, `rounded*`, `border*` | Rectangle (and subclasses); anything exposing an Item `background` routes to that delegate when it is one (all Quick Controls) | warns once per type, skipped — `bg-*` deliberately does **not** set `Text.color` |
| `text-{color}` | Text, TextInput, TextEdit, Controls labels | warns |
| `text-{size}`, `font-*`, `italic`, … | anything with a `font` property | warns |
| `p-*` family | anything declaring `topPadding`/`rightPadding`/… (all Quick Controls; your components can opt in by declaring them) | plain Items have no content padding — warns; use `Loom.space.*` in bindings instead |
| `gap-*` | anything with `spacing` | warns |
| `shadow-*` | any Item (via RectangularShadow) | rectangular silhouette only |
| `fill`, `center`, `pin-*` | any Item, as anchors; inside a Layout `fill` and `center` route to `Layout.*` | `center-x/y` and `pin-*` have no layout form and warn there — use `self-*` |
| `self-*`, `min-w-*`/`max-w-*`/…, `col-span-*` | items inside a RowLayout, ColumnLayout or GridLayout | Qt Quick has no min/max or span off a layout — warns, naming the reason |
| `aspect-*` | any Item; writes `height` from `width` | inside a Layout it sets `Layout.preferredHeight` instead |

## Semantics that differ from CSS

- **`m-*` is context-routed, not a CSS box model.** Inside a Layout
  (Row/Column/GridLayout) it writes the `Layout.*` attached margins — which
  requires the item's *own file* to `import QtQuick.Layouts`, since attached
  types resolve through the document's imports (a warning tells you).
  Everywhere else it writes anchor margins, which only take effect where an
  anchor line is actually set. Positioners (plain Row/Column/Grid/Flow) have
  no per-item margin concept at all.
- **`p-*` is duck-typed**, not a layout primitive. A plain `Item`/`Rectangle`
  does not gain padding by styling it.
- **On a Control, the box utilities land on its `background` delegate.** A
  Control is not a Rectangle, so `bg-*`/`rounded*`/`border*` are written through
  `background.*` instead. Two consequences. The delegate must actually be a
  Rectangle — the built-in styles' are, a hand-written `background: Item {}` is
  not, and that warns as unsupported. And the write takes the property over per
  the binding rule below, so `bg-surface` on a Button replaces the style's own
  down/hover colouring; restore it with `hover:`/`pressed:` variants rather than
  expecting both to apply.
- **`w-full`/`h-full` copy the parent's size** (kept in sync), they do not
  create a constraint system. `fill` anchors instead, which is usually what you
  want; the two are different mechanisms and must not be combined on one item.
- **The layout family routes on the parent's type**, resolved on every apply, so
  reparenting an item between a Layout and anything else moves the write with
  it. Anchoring an item a layout manages is undefined behaviour in Qt, which is
  why this is routing rather than a warning.
- **Shadows are rectangles.** `RectangularShadow` matches the target's
  geometry and corner radii; irregular content casts a rectangular shadow.
- **`tracking-*` resolves against the text size applied in the same string**
  (or the current `font.pixelSize`); it is written as absolute pixels.
- **`transition-*` animates Loom's own writes only** — theme switches, state
  flips, breakpoint changes. It does not animate your bindings or imperative
  writes, first-time application, or restores; `transition-all` covers colors
  and numeric properties, never bools (`visible`) or the managed shadow.

## Property writes vs. bindings

`Lo.style` writes imperatively. The first write to a property tears down any
QML binding the user had on it (standard QML semantics) — permanently.
Removing the class later restores the property's *value* from before the
first write, not the binding. Rule of thumb: a property is either yours (bind
it) or Loom's (style it), never both.

Initial application is deferred one event-loop turn, so `Lo.style` reliably
overrides same-document initial values regardless of declaration order — but
it also means a styled window's very first frame can render pre-style values
if the item is already visible. Set matching initial values for
flicker-critical properties.

## Other boundaries

- Interaction variants need a QML engine; items constructed raw in C++ get
  token access but no `hover:`/`pressed:`.
- `hover:` never fires on touchscreens (there is no cursor); design
  touch-first with `pressed:`.
- The state watcher and shadow are real child items stacked at `z: -1` — code
  walking `children` or `childItems()` will see them. They are children rather
  than siblings so that a positioner or Layout never lays them out as content
  of their own; the cost is that a target with `clip: true` clips its own
  shadow.
- One breakpoint prefix per class; the last one wins. Across classes,
  breakpoints and states rank on separate axes: a state variant outranks a
  breakpoint variant, so `hover:bg-accent` still applies at `md:` widths, and
  `md:hover:` outranks both.
- `Loom.*.value()` runtime lookups are snapshots — they do not re-evaluate on
  theme switches (typed properties and utility strings do).
- Control icons ignore `icon.color` when the source is a file. Qt tints an
  icon item only while it is a mask, which a `.svg`/`.png` source never is, so
  the color is accepted and silently dropped — as is `palette.buttonText`,
  which feeds the same path. Use
  [`Loom.icon()`](tokens.md#icons--loomicon) to recolour the pixels instead.
- Qt's SVG renderer does not implement `currentColor`: it resolves to black
  rather than to an inherited color, so icon sets that stroke with it (Lucide,
  Feather, Bootstrap Icons) render black until something recolours them.

## Vocabulary Tailwind has and Loom still does not

The remaining boundary is mostly structural. A string naming one of these is
reported as unknown rather than approximated with surprising behavior.

| Missing | Why / what to use |
| --- | --- |
| **Container-level alignment** — `items-*`, `justify-*` | Qt Quick Layouts expose alignment per child, through [`self-*`](utilities.md#layout-only), rather than as a container property |
| **Sibling anchors** — `anchors.left: sidebar.right` | a class cannot name another QML `id`; write the anchor in QML |
| **Changing structure** — `flex`, `grid`, column counts | a class cannot change an item's QML type; use `RowLayout`, `ColumnLayout`, `GridLayout`, positioners, or views |
| **Named sibling/peer selectors** | Loom groups walk ancestors. Qt ownership and visual stacking do not provide a stable CSS-like previous-sibling selector |
| **Pseudo-elements and generated content** | declare the Item or Text explicitly |
| **Multiple composed user effects** | filter utilities own the one `Item.layer.effect` slot and therefore require `Lo.effects: true`; compose a custom `MultiEffect` yourself when Loom should not own it |

Loom covers appearance, state, responsive/container conditions, and placement;
ordinary QML remains the structural language.

## Tooling limitations

- Desktop builds are hosted on Linux, macOS, and Windows; Android and iOS have
  hosted emulator/simulator jobs. Embedded builds require a project-owned SDK,
  sysroot, and reachable board, so CI cannot provide a universal target.
- Native desktop packages are DEB, DMG, and MSI. Mobile and embedded targets
  deploy through their platform adapters rather than CPack.
- The AST-based checker sees literal fragments in nested expressions,
  concatenations, and ternaries. A class assembled entirely from runtime data
  cannot be proven statically. See [../tooling/cli.md](../tooling/cli.md).
- **`loom lsp` sees literal class fragments, not arbitrary JavaScript.** It can
  complete and check quoted results in concatenations and ternaries, but not a
  class assembled entirely at run time. See [the LSP command](../tooling/cli.md#loom-lsp).
- **One application per build directory** for `loom dev`; a multi-application
  project selects with `--app`.
