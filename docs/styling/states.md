# State variants

```qml
Rectangle {
    Lo.style: "bg-surface hover:bg-surface-alt pressed:bg-slate-200
               focus:border-accent disabled:opacity-40"
}
```

| Variant | True when | Source |
| --- | --- | --- |
| `hover:` | the pointer is over the item | an engine-created `HoverHandler` |
| `pressed:` | the item is pressed | the target's own `pressed` property where it has one, otherwise a passive `TapHandler` |
| `focus:` | the item has active focus | `Item.activeFocus` |
| `disabled:` | the item is not enabled | `!Item.enabled` |
| `dark:` | the active theme is dark | the token registry — see [theming.md](theming.md) |

Variants compose in any order and combine as **AND**: `hover:pressed:bg-red-500`
needs both. `dark:` is a state variant like the others, so `dark:hover:` works
and means what it looks like.

## Where each state comes from

### `hover:`

Loom attaches a `HoverHandler` to a watcher item. There is no other source —
Qt Quick has no universal `hovered` property, and `MouseArea.hoverEnabled` is
opt-in and would conflict with the target's own mouse handling.

**`hover:` never fires on a touchscreen.** There is no cursor to hover with.
Design touch-first with `pressed:`, and treat `hover:` as desktop enhancement.

### `pressed:`

Two paths, chosen per item:

1. **The target's own `pressed` property**, when it has a `bool pressed` with a
   notify signal — `MouseArea`, every Quick Controls button, and any component
   of yours that declares one. This is the preferred path: it reflects keyboard
   activation and the target's own gesture policy, and adds no items to the
   scene.
2. **A passive `TapHandler`** on the watcher item otherwise, so `pressed:` works
   on a bare `Rectangle`.

The TapHandler uses the `DragThreshold` gesture policy, which keeps it on a
passive grab. The cost is that `pressed` reverts to false if the point drags
past the threshold — which is the behaviour you want for styling anyway.

### `focus:`

Plain `activeFocus`. `focus-visible` semantics — showing a focus ring for
keyboard navigation but not for a mouse click — need to know *how* focus
arrived, which Qt Quick does not report. Out of scope for v1 rather than
approximated badly.

### `disabled:`

`!enabled`. Note that `enabled` is inherited in Qt Quick: disabling a parent
disables its children, and their `disabled:` styling applies too.

## The watcher item

When `hover:` is used, or `pressed:` on a target with no native `pressed`
property, Loom adds **one invisible child item** to the target holding a
`HoverHandler` and a `TapHandler`. It is stacked below every other child
(`z: -1`) and fills the target.

Being at the bottom of the stack is what keeps it from breaking your input:

- the target's own MouseAreas, handlers and interactive children keep every
  event — a TapHandler stacked *above* a MouseArea sibling swallows its clicks
  even on a passive grab, which is why the ordering is not incidental;
- `pressed:` on a bare Rectangle works, because nothing else claims the press;
- `pressed:` does **not** fire on regions covered by a child that exclusively
  grabs the press, such as a MouseArea. Put the variant on the interactive
  element itself, where its native `pressed` property is preferred
  automatically.

Two consequences worth knowing:

- the watcher is a real `QQuickItem` and is visible to code that walks
  `children` or `childItems()`. It has no size effects and does not paint;
- an item created without a QML engine — plain C++ `new QQuickItem` — cannot get
  handlers at all. Tokens still resolve, but `hover:` and `pressed:` never fire,
  and a warning names the item.

Only one watcher is created per item, no matter how many state variants the
string uses. See [performance.md](performance.md) for what it costs.

## Specificity

State variants outrank breakpoint variants, and more states outrank fewer:

```qml
// At any width, hovering wins over the md: rule.
Lo.style: "bg-white hover:bg-black md:bg-red-500"

// Both states must hold; this outranks either alone.
Lo.style: "hover:bg-black pressed:bg-blue-500 hover:pressed:bg-green-500"
```

Between two classes with the same number of state variants, the breakpoint tier
breaks the tie, and then position in the string. The full ordering is in
[utilities.md](utilities.md#specificity--which-rule-wins).

A combination that can never match — `disabled:hover:` on a control that stops
receiving hover events when disabled, for instance — is not detected. It simply
never applies.

## Combining with transitions

State flips are exactly what `transition-*` covers:

```qml
Rectangle {
    Lo.style: "bg-surface hover:bg-accent transition-colors duration-150"
}
```

The write animates from the current value on the way in *and* on the way out,
because both directions are re-applies of the same style. What does not animate
is the first application — see [utilities.md](utilities.md#motion).

## Quick Controls

A Control's own style already colours its hovered and pressed states. When you
write `bg-*` on a Control, that write takes the `background.color` property over
and replaces the style's colouring — so restore it with variants rather than
expecting both to apply:

```qml
Button {
    // Without the hover: rule this button would have no hover feedback at all.
    Lo.style: "bg-surface hover:bg-surface-alt pressed:bg-slate-200 rounded-lg"
}
```

Loom has no variants for Controls-specific states such as `checked`, `down` or
`highlighted`. Bind those yourself:

```qml
CheckBox {
    Lo.style: checked ? "bg-accent rounded" : "bg-surface rounded"
}
```

The string is a binding like any other, and re-compiling it is a cache lookup.
