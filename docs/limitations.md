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
  create a constraint system.
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
- The state watcher and shadow are real child/sibling items — code walking
  `children` or `childItems()` will see them.
- One breakpoint prefix per class; the last one wins.
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
