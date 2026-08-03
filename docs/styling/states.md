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
| `checked:` `down:` `highlighted:` `selected:` | same-named boolean property | Quick Controls or a custom item |
| `editable:` `read-only:` `active:` | same-named boolean property | target property |
| `invalid:` | the item's own `bool invalid` property | target property — Qt has none, but every validating input grows one |
| `focus-within:` | focused item is the target or a descendant | active-focus chain |
| `focus-visible:` | visual focus, or active focus reached by keyboard | native `visualFocus` plus window input modality |
| `rtl:` `ltr:` | application layout direction | `QGuiApplication` |
| `portrait:` `landscape:` | window aspect | target window |
| `window-active:` | target window is active | target window |
| `high-contrast:` | platform requests high contrast | accessibility style hints |
| `motion-reduce:` | reduced motion preference | `Loom.motionPreference` / environment bridge |
| `first:` `last:` `only:` `odd:` `even:` | position among visible siblings | parent child order |

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

`focus:` is plain `activeFocus`. `focus-visible:` prefers a Control's native
`visualFocus`; a shared application input-modality tracker supplies accurate
keyboard-versus-pointer behavior for plain Items.

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

Between two classes with the same number of state variants, responsive
constraints break the tie, and then position in the string. The full ordering is in
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

Controls-specific variants are property-driven and work without a watcher:

```qml
CheckBox {
    Lo.style: "bg-surface checked:bg-accent down:opacity-70 rounded"
}
```

Negation is available for every state (`not-checked:`), and an ancestor can
publish state to descendants with `Lo.group: "menu"` and
`group-hover/menu:`/`group-not-disabled/menu:`. Group lookup uses the nearest
matching attached ancestor and does not install duplicate input handlers.

## States your application owns

The table above is what Loom can observe from Qt. An application has conditions
Qt knows nothing about — a row is syncing, a document is stale, a card is being
dragged — and before these could be variants, the only way to reach them was to
concatenate into the class string:

```qml
Lo.style: "rounded-md " + (row.syncing ? "border-warning" : "border-outline")
```

which had to be repeated on every item that cared, because two items cannot
share one string.

Declare the state in your design file instead:

```json
{
  "states": {
    "syncing": "Has local changes not yet saved to the server"
  }
}
```

and it behaves like any other variant — including `not-`, `group-`, negation,
composition and specificity:

```qml
Box {
    Lo.group: "row"
    Lo.style: "border-outline syncing:border-warning hover:syncing:border-danger"

    Text { Lo.style: "group-syncing/row:text-warning" }
}
```

### Supplying the value

Two ways, and components usually want the second.

**`Lo.states`** — a map, at the call site:

```qml
Box {
    Lo.states: ({ syncing: model.dirty, stale: model.age > 3600 })
    Lo.style: "syncing:border-warning stale:opacity-60"
}
```

The map is an ordinary QML binding, so that is the whole reactivity story. An
apply is scheduled only when the resolved set of active states actually
changes, not on every re-evaluation.

**A bool property of the same name** — inside a component:

```qml
// MyRow.qml
Box {
    property bool syncing: false
    Lo.style: "border-outline syncing:border-warning"
}
```

Duck-typed exactly the way `checked` and `readOnly` are, so nothing at the call
site has to restate it. State names are kebab-case and the property is
camelCase: `not-found:` reads `notFound`.

### Rules

- **Declared states must be declared.** An undeclared name in `Lo.states` warns,
  and an undeclared variant is reported by `loom lint` as an unknown class.
  That is deliberate: accepting arbitrary names would mean every typo became
  "maybe that is a state", which is the exact bug the checker exists to catch.
  Because the design file is the source of truth, `loom style`, `loom lint` and
  the LSP all learn your states with no extra configuration — completion, typo
  suggestions and hovers included.
- **They rank like built-in states.** There is no reading under which
  `syncing:` is inherently weaker or stronger than `hover:`, so at equal depth
  the later class wins and `hover:syncing:` beats either alone.
- **A name that collides with a built-in variant is rejected** rather than
  silently shadowed, as are the `not-`, `group-`, `theme-`, `min-` and `max-`
  prefixes, which already spell composed variants.
- **At most 32 per project.** The bit has to live somewhere.
- Names are lower-case letters, digits and dashes, starting with a letter.

