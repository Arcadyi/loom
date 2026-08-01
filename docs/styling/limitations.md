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

## Vocabulary Tailwind has and loom does not

Worth knowing before you reach for a class that is not there. None of these are
in the 1702, and a string naming one is reported as unknown.

| Missing | What to do instead |
| --- | --- |
| **Container-level alignment** — `items-*`, `justify-*` | Qt Quick Layouts have no container alignment property; only the per-child `Layout.alignment` that [`self-*`](utilities.md#layout-only) writes |
| **Sibling anchors** — `anchors.left: sidebar.right` | no class can name another item's `id`. Write it in QML |
| **Grid and flex containers** — `flex`, `grid` | a class cannot change an item's *type*. Use `RowLayout`/`ColumnLayout`/`GridLayout` and style the children |
| **Positional offsets** — `top-4`, `inset-x-2` | `pin-t mt-4`: the [pin classes](utilities.md#layout) set the anchor, `m-*` supplies the offset |
| **Fractional widths** — `w-1/2` | a Layout with `fill-x`, or bind `width` |
| **Arbitrary values** — `p-[13]`, `bg-[#7c5cff]` | add the value to the [design token file](configuration.md), or use `Loom.space.*` in a binding |
| **Negative values** — `-mt-4` | write the anchor margin directly |
| **Text layout** — `text-center`, `truncate`, `line-clamp-*`, `uppercase`, `leading-*` alone | the underlying Text properties: `horizontalAlignment`, `elide`, `maximumLineCount`, `font.capitalization`, `lineHeight` |
| **Font family** — `font-sans`, `font-mono` | `font.family`. There is no font token in loom at all |
| **Transforms** — `rotate-*`, `scale-*`, `translate-*`, `origin-*` | `rotation`, `scale`, `transform` |
| **Overflow and cursor** — `overflow-hidden`, `cursor-pointer` | `clip: true`; a `HoverHandler` with `cursorShape` |
| **Rings, gradients, filters** — `ring-*`, `bg-gradient-*`, `blur-*` | a sibling Rectangle; `Gradient`; `QtQuick.Effects.MultiEffect` |
| **More variants** — `group-hover:`, `first:`, `last:`, `checked:`, `max-md:`, `not-*`, `rtl:` | bind the style string to the condition, as in [the cookbook](cookbook.md#styling-quick-controls) |
| **`@apply` / named class sets** | no component layer exists; factor the QML into a component instead |

The general shape: loom covers **appearance** and **placement**, but not
*structure* — it can anchor and align an item, and cannot change what kind of
item it is or how its container arranges children. It also offers no escape
hatch for values outside the token scales. Where a utility is missing,
the typed `Loom.*` API and ordinary QML properties are the intended answer, not
a workaround.

## Tooling limitations

- **Only `desktop` builds.** `--target android|ios|embedded` is accepted and
  then rejected: only `loom doctor` inspects those toolchains today. See
  [../tooling/platforms.md](../tooling/platforms.md).
- **Linux is the validated platform.** Windows and macOS paths exist in the
  code and are not covered by CI.
- **`loom deploy` does not bundle Qt**, and has no option to. See
  [why](../tooling/platforms.md#why-qt-is-not-bundled).
- **The class checker reads string literals**, so it cannot see a class assembled
  at run time, and it reports any string literal inside a `Lo.style` binding —
  including one that is not a class at all. See
  [../tooling/cli.md](../tooling/cli.md).
- **No editor completion inside `Lo.style`** in any editor today, though
  `loom style --catalogue` emits the data an integration would need.
- **One application per build directory** for `loom dev`; a multi-application
  project selects with `--app`.
