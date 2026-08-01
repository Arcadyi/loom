# Utility strings — `Lo.style`

Attach a space-separated utility string to any `Item`:

```qml
Rectangle {
    Lo.style: "bg-surface border border-outline rounded-lg p-4 hover:bg-surface-alt md:p-6"
}
```

Strings compile once per unique value (a process-wide cache), so binding
`Lo.style: checked ? A : B` costs one hash lookup per switch after the first.
Unknown classes warn once per unique string (logging category `loom.style`)
and are skipped — never fatal.

## Grammar

```
class    := (variant ":")* utility modifier?
variant  := sm | md | lg | xl | hover | pressed | focus | disabled | dark
modifier := "/" 0..100        // colour opacity; bg-*, text-*, border-* only
```

Variant prefixes compose in any order (`md:hover:bg-…` ≡ `hover:md:bg-…`).
See [responsive.md](responsive.md) and [states.md](states.md).

## Colour opacity — `bg-surface/70`

A trailing `/0`–`/100` scales a colour's alpha, as in Tailwind:

```qml
Rectangle { Lo.style: "bg-surface/70 border border-outline/25" }
```

It applies to the three colour families — `bg-*`, `text-*`, `border-*` — and
composes with every variant (`hover:bg-accent/80`).

The modifier travels with the *token*, not with a resolved value, so a theme
switch re-resolves the colour and re-applies the alpha. It **scales** the
token's own alpha rather than replacing it, so an already-translucent token
composes instead of being overridden.

Anywhere it would be meaningless it is rejected rather than ignored:
`w-full/70` and `p-4/70` warn as unknown classes, as do out-of-range or
non-numeric values like `/101` and `/half`.

## Utilities

| Utility | Effect | Notes |
| --- | --- | --- |
| `bg-{color}` | background color | Rectangle, or a Control's `background` delegate — see [limitations](limitations.md) |
| `text-{color}` | text color | Text/TextInput/TextEdit (and Controls labels) |
| `text-{size}` | `font.pixelSize` (+ fixed `lineHeight` where supported) | sizes: `xs sm base lg xl 2xl 3xl 4xl`; size keys win over colors |
| `font-{weight}` | `font.weight` | `thin…black` |
| `italic` / `not-italic` | `font.italic` | |
| `underline` / `no-underline` | `font.underline` | |
| `line-through` | `font.strikeout` | |
| `tracking-{k}` | `font.letterSpacing` | em-relative to the applied text size |
| `p- px- py- pt- pr- pb- pl-{n}` | per-side padding properties | duck-typed: Controls and components declaring them |
| `m- mx- my- mt- mr- mb- ml-{n}` | `Layout.*` margins inside a Layout, anchor margins otherwise | re-routes on reparent; see [limitations](limitations.md) |
| `gap-{n}` | `spacing` | Row/Column/Grid/Flow, Layouts, anything with `spacing` |
| `w-{n}` `h-{n}` `size-{n}` | width/height from the spacing scale | |
| `w-full` `h-full` | track the parent's size | |
| `rounded[-{k}]` | `radius` | bare `rounded` = 4px; `rounded-full` is 9999, which Qt clamps to a circle (square item) or a pill |
| `rounded-{t\|r\|b\|l\|tl\|tr\|bl\|br}[-{k}]` | per-corner radius | |
| `border[-{n}]` | `border.width` | bare `border` = 1; any number accepted |
| `border-{color}` | `border.color` | |
| `opacity-{k}` | `opacity` | `0 5 10 … 100` |
| `visible` / `invisible` | `visible` | |
| `shadow[-{k}]` | managed drop shadow | `sm base md lg xl 2xl none`; see below |
| `transition[-{colors\|opacity\|all\|none}]` | animates covered property changes | bare `transition` covers colors + opacity |
| `duration-{k}` | transition duration | duration tokens: `75…1000` ms; default 150 |
| `ease-{linear\|in\|out\|in-out}` | transition easing | default `ease-in-out` |

`{color}` is any palette key (`blue-500`), semantic name (`surface`,
`accent`), `white`, `black`, `transparent`, or a config-defined color.
`{n}` is a spacing-scale key (`4` = 16px, `0.5` = 2px, config keys included).

## Shadows

`shadow-*` does not write a property: Loom creates a `RectangularShadow`
sibling (QtQuick.Effects) stacked just below the target, bound to its
geometry, corner radii, visibility and opacity. `shadow-none` (or removing
the class) deletes it. Non-rectangular content casts a rectangular shadow —
that is the honest limit of the approach.

## Transitions

```qml
Rectangle {
    Lo.style: "bg-surface hover:bg-surface-alt transition-colors duration-150"
}
```

When a re-apply changes a covered property (theme switch, state flip,
breakpoint change), the write animates from the current value instead of
snapping. Coverage: `transition-colors` → color-valued properties;
`transition-opacity` → `opacity`; `transition-all` → colors plus every
numeric property; bare `transition` → colors + opacity. `transition-none`
(usable with variants: `dark:transition-none`) disables animation; an
interrupted animation retargets smoothly from wherever it is. The *first*
application always snaps — creation is not a transition — and so do
restores when a class stops applying.

## Conflict rules

- **`Lo.style` wins** for every property it currently manages, and re-resolves
  it on every re-apply (theme switch, state change, resize). It does not police
  the property in between: an apply skips a write whose value already matches,
  so an imperative write from elsewhere survives until something changes what
  Loom wants the value to be. A property is either yours or Loom's.
- The first write to a property saves the previous value. When a class stops
  applying (dynamic string, cleared style), the saved value is restored.
- A user *binding* on a managed property is torn down by the first write and
  cannot be restored — only its value at that moment is.
- Between rules targeting the same property: more variant prefixes win;
  at equal counts the later class wins.
