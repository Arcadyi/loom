# Utility strings — `Lo.style`

`Lo.style` is an attached property available on any `Item`. It takes a
space-separated list of utility classes and writes the corresponding QML
properties on the item.

```qml
import QtQuick
import Loom

Rectangle {
    Lo.style: "bg-surface border border-outline rounded-lg p-4 hover:bg-surface-alt md:p-6"
}
```

The vocabulary is generated from the live token, theme, breakpoint, container,
and recipe registries. This document lists the families and their resolved
properties.
`loom style --catalogue` emits the same list as JSON, generated from the
parser's own tables — see [../tooling/cli.md](../tooling/cli.md).

Utilities write *real QML properties on real types*. There is no box model
underneath: `p-4` sets `topPadding` and its siblings on a type that has them,
and warns on a type that does not. What that costs you is in
[limitations.md](limitations.md).

## Contents

- [Grammar](#grammar)
- [How a string becomes property writes](#how-a-string-becomes-property-writes)
- [Colour](#colour) · [Typography](#typography) · [Spacing](#spacing--padding-margin-gap)
- [Size](#size) · [Border and radius](#border-and-radius) · [Effects](#effects)
- [Layout](#layout) · [Motion](#motion)
- [Scales](#scales) — the numbers behind every key
- [Variants](#variants) and [specificity](#specificity--which-rule-wins)
- [Conflict rules](#conflict-rules--lostyle-versus-your-own-code)
- [Unknown classes](#unknown-classes)

---

## Grammar

```
class    := variant* ( utility | "@" recipe ) modifier?
variant  := ( breakpoint | "max-" breakpoint | "min-[" px "]" | "max-[" px "]"
            | "@" container ( "/" name )?
            | state | "not-" state | "group-" state ( "/" name )?
            | "theme-" theme ) ":"
modifier := "/" ( 0 .. 100 )        // colour opacity on every colour utility
```

### Writing a long class string

Classes are separated by whitespace, and a QML template literal can carry
newlines — so a long list does not need a `+` at the end of every line:

```qml
Rectangle {
    Lo.style: `p-4 bg-surface rounded-lg
               hover:bg-blue-600 hover:shadow-md
               md:p-6 md:text-lg`
}
```

`loom lint`, `loom style` and the editor read these exactly as they read a
plain string. A template with a `${...}` substitution in it is a computed
string and is not read offline, the same as the dynamic half of a
concatenation.

Variant prefixes compose in any order and combine as **AND** —
`md:hover:bg-accent` is identical to `hover:md:bg-accent`, and needs both a
window at least 768 px wide and a hovered item. At most one breakpoint prefix is
meaningful per class; if you write two, the later one wins.

Classes are separated by any run of spaces. The string may be a binding —
`Lo.style: checked ? "bg-accent" : "bg-surface"` — and re-applies when it
changes.

Typed arbitrary values use brackets: `p-[13]`, `w-[240px]`, `bg-[#7c5cff]`,
`opacity-[0.72]`, `rotate-[17]`. Negative values are accepted where the
underlying concept supports them (`-mt-4`, `-translate-x-1/2`, `-rotate-12`).
Fractional `w-1/2`, `h-2/3`, and translation classes stay live as sizes change.

Named recipes from the design file expand at compile time. `@card` inserts the
recipe, and a call-site variant distributes across every expanded rule:
`hover:@card`. Recipes can include other recipes; cycles, missing recipes,
nesting past 32, and expansions past 4096 rules are rejected.

## How a string becomes property writes

Worth knowing, because most surprises come from one of these five steps.

1. **Compile.** The string is parsed into rules, once per unique string, into a
   process-wide cache. Binding `Lo.style` to an expression that flips between
   two values costs one hash lookup per flip after the first compile. An unknown
   class is dropped here, with a warning.
2. **Filter.** On each apply, rules whose viewport/container bounds do not
   match, or whose target/group state conditions are not satisfied, are
   discarded.
3. **Resolve.** Surviving rules turn token *names* into values against the
   active theme. Rules store names, never resolved values — which is why a theme
   switch re-applies without recompiling.
4. **Rank.** Where two rules target the same property,
   [specificity](#specificity--which-rule-wins) decides.
5. **Write.** The first write to a property saves its previous value. Properties
   no longer wanted are restored to what was saved.

The first apply is deferred by one event-loop turn. That is deliberate — it puts
the write *after* the item's own initial property assignments, so `Lo.style`
wins regardless of declaration order — and it has one consequence worth knowing:
a styled item that is already visible can render one frame with its pre-style
values. Set matching initial values for flicker-critical properties.

---

## Colour

| Class | Writes | On |
| --- | --- | --- |
| `bg-{color}` | `color` | Rectangle |
| | `background.color` | any type with an `Item background` that is a Rectangle — every Quick Control |
| `text-{color}` | `color` | Text, TextInput, TextEdit, and Controls exposing a `color` |
| `border-{color}` | `border.color` | Rectangle, or a Control's background delegate |

`{color}` is any of:

- a **palette** key — 22 hues × 11 shades, `slate-50` … `rose-950`;
- a **semantic** key — `background surface surface-alt outline foreground muted
  faint accent accent-hover on-accent danger on-danger success warning`, which
  resolve through the active theme ([theming.md](theming.md));
- `white`, `black`, `transparent`;
- any colour your [design token file](configuration.md) defines.

The 22 hues are `slate gray zinc neutral stone red orange amber yellow lime
green emerald teal cyan sky blue indigo violet purple fuchsia pink rose`; the 11
shades are `50 100 200 300 400 500 600 700 800 900 950`. The values are
Tailwind's.

`bg-*` deliberately does **not** fall back to `Text.color`. A background utility
silently colouring text would be a worse outcome than a warning.

### Colour opacity — `bg-surface/70`

A trailing `/0`–`/100` scales a colour's alpha:

```qml
Rectangle { Lo.style: "bg-surface/70 border border-outline/25" }
```

It applies to background, text, border, ring, and gradient-stop colours, and
composes with every variant (`hover:bg-accent/80`,
`dark:text-foreground/50`).

Three properties worth stating exactly:

- It **scales** the token's own alpha rather than replacing it, so a token that
  is already translucent composes with the modifier instead of being overridden.
  `/50` on a token at alpha 0.4 yields 0.2.
- It travels with the **token**, not with a resolved value, so a theme switch
  re-resolves the colour and re-applies the alpha.
- Anywhere it would be meaningless it is **rejected**, not ignored. `w-full/70`
  and `p-4/70` are unknown classes, as are `/101` and `/half`. Silently dropping
  a modifier is how "why is this not translucent" becomes a half-hour.

## Typography

| Class | Writes | Value |
| --- | --- | --- |
| `text-{size}` | `font.pixelSize` | see the [text scale](#text) |
| | `lineHeight`, `lineHeightMode` | also written where the type has both; the mode is `Text.FixedHeight` |
| `font-{weight}` | `font.weight` | `thin` 100 … `black` 900 |
| `font-{family}` | `font.family` | `sans`, `serif`, `mono`, or a design token |
| `tracking-{k}` | `font.letterSpacing` | em × the applied pixel size, in absolute px |
| `italic` / `not-italic` | `font.italic` | `true` / `false` |
| `underline` / `no-underline` | `font.underline` | `true` / `false` |
| `line-through` | `font.strikeout` | `true` |
| `text-left` / `text-center` / `text-right` / `text-justify` | `horizontalAlignment` | Qt alignment |
| `text-ellipsis` / `text-clip` / `truncate` | `elide` and, for `truncate`, no wrap | text overflow |
| `line-clamp-{n}` / `line-clamp-none` | `maximumLineCount` | line limit |
| `uppercase` / `lowercase` / `capitalize` / `normal-case` | `font.capitalization` | capitalization |
| `whitespace-normal` / `whitespace-nowrap` / `wrap-anywhere` | `wrapMode` | wrapping |
| `leading-{size}` | `lineHeight` | a text-size token's line height |

Everything here except `text-{color}` is keyed off the target having a `font`
property, so it works on Text, TextInput, TextEdit, and every Quick Control that
exposes one. Other types warn.

`text-{size}` and `text-{color}` share a prefix. **Size keys win**: `text-xl` is
20 px type, not a colour, and there is no colour named `xl` to collide with.

`tracking-*` is em-relative and resolves against the pixel size *this apply pass
is setting*, whether the `text-{size}` class comes before or after it in the
string. It is written as absolute pixels, so an item whose size later changes by
other means will not re-derive its letter spacing until something triggers a
re-apply.

## Spacing — padding, margin, gap

| Class | Writes |
| --- | --- |
| `p-{n}` | `topPadding` `rightPadding` `bottomPadding` `leftPadding` |
| `px-{n}` / `py-{n}` | the horizontal / vertical pair |
| `pt-` `pr-` `pb-` `pl-{n}` | one side |
| `m-{n}` `mx-` `my-` `mt-` `mr-` `mb-` `ml-{n}` | see below |
| `gap-{n}` | `spacing` |

**Padding is duck-typed.** Any type declaring `topPadding` and friends opts in —
all Quick Controls, and your own components if you declare
`property real topPadding`. A plain `Item` or `Rectangle` has no content padding
in Qt Quick and does not gain any by being styled; it warns. Use `Loom.space.*`
in your own bindings there.

**Margin is context-routed**, and this is the sharpest edge in the vocabulary:

- inside a `RowLayout`, `ColumnLayout` or `GridLayout`, `m-*` writes the
  `Layout.topMargin` family — which requires the item's **own file** to
  `import QtQuick.Layouts`, because attached types resolve through the
  document's imports. A warning names the item when it does not;
- everywhere else it writes `anchors.topMargin` and friends, which take effect
  only where an anchor line is actually set;
- plain positioners (`Row`, `Column`, `Grid`, `Flow`) have no per-item margin
  concept at all. Use `gap-*` on the positioner instead.

The routing is resolved on each apply, so reparenting an item between a Layout
and anything else re-routes its margins.

`gap-{n}` works on anything with a `spacing` property: the positioners, the
Layouts, `ListView`, and Controls built on them.

## Size

| Class | Writes | Notes |
| --- | --- | --- |
| `w-{n}` `h-{n}` | `width` / `height` | from the [spacing scale](#spacing) |
| `size-{n}` | both | |
| `w-full` `h-full` | `width` / `height` = the parent's | kept in sync as the parent resizes |
| `w-{n}/{d}` `h-{n}/{d}` | parent size × the fraction | kept in sync |

`w-full` copies the parent's width and keeps copying it; it is not a constraint
system and not an anchor. Fractions such as `w-1/2` and `h-2/3` are also live
copies of a parent dimension. For constraint-based proportional layout, use a
Layout or a binding.

## Border and radius

| Class | Writes | Value |
| --- | --- | --- |
| `border` | `border.width` | 1 |
| `border-{n}` | `border.width` | the number, e.g. `border-2` → 2 |
| `border-{color}` | `border.color` | |
| `rounded` | `radius` | 4 (the `base` step) |
| `rounded-{k}` | `radius` | see the [radius scale](#radius) |
| `rounded-{tl\|tr\|br\|bl}[-{k}]` | one corner: `topLeftRadius`, … | |
| `rounded-{t\|r\|b\|l}[-{k}]` | the two corners of one edge | |

`border-{n}` takes any non-negative finite number, including fractions
(`border-1.5`). Negatives, `nan` and `inf` are rejected as unknown classes
rather than written into the target.

`rounded-full` is 9999 px, which Qt clamps to half the shorter side — a circle
on a square item, a pill on a wide one.

On a Control these land on the `background` delegate, which must itself be a
Rectangle. The built-in styles' backgrounds are; a hand-written
`background: Item {}` is not, and warns as unsupported.

## Effects

| Class | Effect |
| --- | --- |
| `opacity-{k}` | `opacity`, from the [opacity scale](#opacity) |
| `visible` | `visible: true` |
| `hidden` | `visible: false` — removed from positioner layout |
| `invisible` | `opacity: 0` — keeps its layout slot |
| `shadow[-{k}]` | a managed drop shadow — see below |
| `shadow-none` | removes it |

### Transforms, cursors, rings, gradients, and filters

| Family | Examples | Runtime form |
| --- | --- | --- |
| transform | `rotate-45`, `scale-110`, `translate-x-1/2`, `origin-bottom-right` | native Item properties plus a managed `Translate` |
| cursor | `cursor-pointer`, `cursor-text`, `cursor-grab` | the Item's native cursor |
| ring | `ring`, `ring-4`, `ring-accent/50` | a managed outline child |
| gradient | `bg-linear-to-r from-blue-500 via-violet-500 to-pink-500` | a managed Rectangle gradient |
| filter | `blur-md`, `brightness-125`, `contrast-75`, `saturate-150`, `grayscale` | `MultiEffect` through `Item.layer.effect` |

Filter utilities require `Lo.effects: true`. This explicit ownership opt-in is
what permits Loom to replace `Item.layer.effect`; clearing the classes or the
opt-in restores the previous effect and layer-enabled value. Rings and
gradients manage their own visual objects and do not take layer-effect
ownership. Rectangle gradients are axis-aligned, so diagonal directions use
the nearest deterministic axis.

### Shadows

`shadow-*` does not write a property. Loom creates a `RectangularShadow`
(`QtQuick.Effects`) as a **child of the target at `z: -1`**, bound to its
geometry and corner radii. Negative-z children render before their parent's own
content, so it draws beneath the target's background.

It is a child rather than a sibling on purpose: a sibling lives in the target's
parent, and when that parent is a positioner or a Layout, the shadow claims a
layout slot of its own and its position bindings fight the positioner's writes.
As a child it is outside every layout. The cost is that a target with
`clip: true` clips its own shadow.

The radius follows the same delegate resolution as `rounded-*`, so a rounded
Control casts a rounded shadow.

Shadows are rectangular: `RectangularShadow` matches the target's geometry and
corner radii, so irregular content casts a rectangular shadow. That is the
honest limit of the approach. Values are in the [shadow scale](#shadow).

## Layout

Qt Quick places items two ways — **anchors**, which work on any Item, and the
**`QtQuick.Layouts` attached properties**, which only mean anything inside a
`RowLayout`, `ColumnLayout` or `GridLayout`. These classes resolve to whichever
one applies, decided per item on every apply, so a reparent re-routes them.

That is not a convenience: anchoring an item a layout manages is undefined
behaviour, and Qt warns about it. Routing is how `fill` stays correct in both
worlds.

| Class | Outside a layout | Inside a layout |
| --- | --- | --- |
| `fill` | `anchors.fill: parent` | `Layout.fillWidth` + `Layout.fillHeight` |
| `fill-x` | `anchors.left` + `anchors.right` | `Layout.fillWidth` |
| `fill-y` | `anchors.top` + `anchors.bottom` | `Layout.fillHeight` |
| `center` | `anchors.centerIn: parent` | `Layout.alignment: Qt.AlignCenter` |
| `center-x` | `anchors.horizontalCenter` | — use `self-*` |
| `center-y` | `anchors.verticalCenter` | — use `self-*` |
| `pin-t` `pin-r` `pin-b` `pin-l` | `anchors.{top,right,bottom,left}` | — use `self-*` |

`fill` is the equivalent of flexbox's `flex-1` / `grow`; there is no second
name for it.

**Anchors are what finally make `m-*` do something.** Anchor margins only take
effect where an anchor line is set, so `fill m-4` is an item filling its parent
inset by 16 px, and `pin-l ml-6` is an item on the left edge, 24 px in. The two
families were designed to compose.

```qml
Rectangle {
    // Fills the parent, inset by 16px on every side.
    Lo.style: "bg-surface rounded-lg fill m-4"
}
```

### Layout-only

These have no anchors equivalent — Qt Quick has no min/max or span concept off
a layout — so outside one they warn and skip, naming the reason.

| Class | Writes |
| --- | --- |
| `self-start` `self-center` `self-end` `self-stretch` | `Layout.alignment`; `self-stretch` is Qt's default, which it spells as no alignment |
| `min-w-{n}` `max-w-{n}` `min-h-{n}` `max-h-{n}` | `Layout.minimumWidth`, `maximumWidth`, `minimumHeight`, `maximumHeight`, from the [spacing scale](#spacing) |
| `col-span-{n}` `row-span-{n}` | `Layout.columnSpan` / `rowSpan`; a whole number of cells, at least 1 |

`self-*` writes the whole alignment at once rather than composing per-axis
flags. Two rules on one flags property would resolve by
[specificity](#specificity--which-rule-wins) rather than merging, so a
decomposed pair would silently lose an axis. For finer control — say
`Qt.AlignLeft | Qt.AlignVCenter` — bind `Layout.alignment` directly.

Anything writing `Layout.*` needs the item's **own file** to
`import QtQuick.Layouts`, because attached types resolve through the document's
imports. A warning names the item when it does not. This is the same constraint
`m-*` already has.

### Aspect ratio

| Class | Ratio |
| --- | --- |
| `aspect-square` | 1 / 1 |
| `aspect-video` | 16 / 9 |
| `aspect-{n}/{m}` | any positive `n / m`, e.g. `aspect-4/3` |

Width drives height: the item's height is set to `width / ratio` and re-derived
whenever the width changes. Inside a layout it writes `Layout.preferredHeight`
instead, because a layout owns its children's geometry.

### What layout utilities do not do

- **No sibling anchors.** `anchors.left: sidebar.right` has no class form — a
  class string cannot name another item's `id`. Write it in QML.
- **No container-level alignment.** Tailwind's `items-*` and `justify-*` go on
  the container; Qt Quick Layouts have no such property, only the per-child
  `Layout.alignment` that `self-*` writes.
- **No offsets of their own.** There is no `pin-l-4`; use `pin-l ml-4`.
- **`fill` and `w-full` are different mechanisms** and must not be combined.
  `fill` anchors; `w-full` copies the parent's width imperatively. Pick one.

## Motion

| Class | Effect |
| --- | --- |
| `transition` | animate colour and opacity changes |
| `transition-colors` | colour-valued properties only |
| `transition-opacity` | `opacity` only |
| `transition-all` | colours plus every numeric property |
| `transition-none` | animate nothing |
| `duration-{k}` | animation duration; default 150 ms |
| `ease-{linear\|in\|out\|in-out}` | easing curve; default `in-out` |

```qml
Rectangle {
    Lo.style: "bg-surface hover:bg-surface-alt transition-colors duration-150 ease-out"
}
```

When a re-apply changes a covered property — a theme switch, a state flip, a
breakpoint change — the write animates from the current value instead of
snapping. An interrupted animation retargets smoothly from wherever it is.

Four things transitions do **not** cover:

- the **first** application, which always snaps: creation is not a transition;
- **restores**, when a class stops applying;
- your own bindings and imperative writes — `transition-*` animates Loom's
  writes only;
- booleans (`visible`) and the managed shadow, even under `transition-all`.

`transition-none` composes with variants, so `dark:transition-none` disables
motion in one theme.

`Loom.motionPreference` can be `SystemMotion`, `ReduceMotion`, or `FullMotion`.
Reduced motion disables every Loom transition and activates the
`motion-reduce:` variant. System mode uses `LOOM_REDUCE_MOTION=1` as the
cross-platform bridge until Qt exposes an OS reduced-motion style hint.

---

## Scales

Every number loom will write, so you do not have to open the source to find out
what `p-7` is.

### Spacing

Used by `p-* m-* gap-* w-* h-* size-*`. Tailwind's scale: the key times 4 px.

| Key | px | Key | px | Key | px | Key | px |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `0` | 0 | `4` | 16 | `12` | 48 | `40` | 160 |
| `0.5` | 2 | `5` | 20 | `14` | 56 | `44` | 176 |
| `1` | 4 | `6` | 24 | `16` | 64 | `48` | 192 |
| `1.5` | 6 | `7` | 28 | `20` | 80 | `52` | 208 |
| `2` | 8 | `8` | 32 | `24` | 96 | `56` | 224 |
| `2.5` | 10 | `9` | 36 | `28` | 112 | `60` | 240 |
| `3` | 12 | `10` | 40 | `32` | 128 | `64` | 256 |
| `3.5` | 14 | `11` | 44 | `36` | 144 | `72` | 288 |
| | | | | | | `80` | 320 |
| | | | | | | `96` | 384 |

34 steps. A design token file can add more; it cannot remove these.

### Text

`text-{size}` writes both numbers where the type supports it.

| Key | `font.pixelSize` | `lineHeight` |
| --- | --- | --- |
| `xs` | 12 | 16 |
| `sm` | 14 | 20 |
| `base` | 16 | 24 |
| `lg` | 18 | 28 |
| `xl` | 20 | 28 |
| `2xl` | 24 | 32 |
| `3xl` | 30 | 36 |
| `4xl` | 36 | 40 |

### Font weight

`thin` 100, `extralight` 200, `light` 300, `normal` 400, `medium` 500,
`semibold` 600, `bold` 700, `extrabold` 800, `black` 900.

### Tracking

Letter spacing in em, resolved against the applied pixel size.

| Key | em |
| --- | --- |
| `tighter` | −0.05 |
| `tight` | −0.025 |
| `normal` | 0 |
| `wide` | 0.025 |
| `wider` | 0.05 |

### Radius

| Key | px | Key | px |
| --- | --- | --- | --- |
| `none` | 0 | `xl` | 12 |
| `sm` | 2 | `2xl` | 16 |
| `base` | 4 | `3xl` | 24 |
| `md` | 6 | `full` | 9999 |
| `lg` | 8 | | |

### Shadow

Single-layer approximations of the Tailwind box shadows. The colour is black at
the listed alpha; the horizontal offset is always 0.

| Key | Y offset | Blur | Spread | Alpha |
| --- | --- | --- | --- | --- |
| `none` | 0 | 0 | 0 | 0 % |
| `sm` | 1 | 2 | 0 | 5 % |
| `base` | 1 | 3 | 0 | 10 % |
| `md` | 4 | 6 | −1 | 10 % |
| `lg` | 10 | 15 | −3 | 10 % |
| `xl` | 20 | 25 | −5 | 10 % |
| `2xl` | 25 | 50 | −12 | 25 % |

### Opacity

`0 5 10 15 20 25 30 35 40 45 50 55 60 65 70 75 80 85 90 95 100`, mapping to
0.0–1.0.

### Duration

`75 100 150 200 300 500 700 1000`, in milliseconds.

### Easing

Cubic beziers matching the CSS timing functions.

| Key | Control points |
| --- | --- |
| `linear` | 0, 0, 1, 1 |
| `in` | 0.4, 0, 1, 1 |
| `out` | 0, 0, 0.2, 1 |
| `in-out` | 0.4, 0, 0.2, 1 |

### Breakpoints

`sm` 640, `md` 768, `lg` 1024, `xl` 1280, `2xl` 1536 px. See
[responsive.md](responsive.md).

---

## Variants

| Prefix | Applies when | Detail |
| --- | --- | --- |
| viewport | `sm:`, `2xl:`, `max-md:`, `min-[900px]:`, `max-[1200px]:` | [responsive.md](responsive.md) |
| container | `@md:`, `@max-lg:`, `@md/sidebar:` | nearest `Lo.container` | [responsive.md](responsive.md) |
| interaction | `hover:`, `pressed:`, `focus:`, `focus-visible:`, `focus-within:` | [states.md](states.md) |
| control | `checked:`, `down:`, `selected:`, `editable:`, `read-only:`, `active:` | [states.md](states.md) |
| environment | `dark:`, `theme-violet:`, `rtl:`, `portrait:`, `window-active:`, `high-contrast:`, `motion-reduce:` | state and theme |
| structural | `first:`, `last:`, `only:`, `odd:`, `even:` | visible sibling position |
| negation/group | `not-disabled:`, `group-hover:`, `group-checked/menu:` | [states.md](states.md) |

## Specificity — which rule wins

When two classes in one string write the same property, loom ranks them on
**two independent axes**, states outranking breakpoints:

1. how many **state** variants the class has (`hover:`, `dark:`, …);
2. then its responsive constraint depth and exact viewport/container bounds;
3. then position in the string — later wins.

```qml
// At 1024 px: red normally, black while hovered.
Lo.style: "bg-white hover:bg-black md:bg-red-500"

// md:hover: outranks hover: — the responsive condition breaks the tie between two
// rules that both carry one state variant.
Lo.style: "hover:bg-black md:hover:bg-blue-500"
```

States outrank breakpoints because a state is a *transient* condition that
should override the static appearance at any width. Ranking both on a single
count of prefixes — which loom did before 0.2.1 — made the later-written class
win every tie, so a `md:` rule silently disabled every `hover:` rule for the
same property above 768 px.

## Conflict rules — `Lo.style` versus your own code

- **`Lo.style` owns every property it currently manages** and re-resolves it on
  each re-apply. It does not police the property in between: an apply skips a
  write whose value already matches, so an imperative write from elsewhere
  survives until something changes what Loom wants the value to be.
- **The first write saves the previous value.** When a class stops applying — a
  dynamic string changes, the style is cleared — the saved value is restored.
- **A binding on a managed property is destroyed by the first write** and cannot
  be restored; only its value at that moment is. That is standard QML semantics
  for an imperative write, not something loom adds.

The rule of thumb that follows: **a property is either yours or Loom's**. Bind
it, or style it, never both.

## Unknown classes

An unrecognised class is dropped with a warning on the `loom.style` logging
category, once per unique string. It is never fatal — a typo costs you one
missing rule, not a broken scene.

Because that warning is easy to miss in a busy log, and because a mistyped class
is invisible to both the compiler and qmllint, check them at build time:

```console
$ loom lint
qml/Main.qml:44: unknown utility class 'bg-brnad-500'
loom: 1 unknown class(es) in 1 file(s)
```

`loom lint` runs `qmllint` and this check together. See
[../tooling/cli.md](../tooling/cli.md) for what the checker can and cannot see
in a QML file.
