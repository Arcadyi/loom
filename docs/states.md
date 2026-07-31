# State variants

```qml
Rectangle {
    Lo.style: "bg-surface hover:bg-surface-alt pressed:bg-slate-200
               focus:border-accent disabled:opacity-40"
}
```

| Variant | Source | Notes |
| --- | --- | --- |
| `hover:` | engine-created `HoverHandler` | false on touchscreens — pair with `pressed:` |
| `pressed:` | the target's own bool `pressed` property when it has one (MouseArea, Controls buttons, custom components); otherwise an engine-created passive `TapHandler` | the TapHandler path un-presses when the point drags away |
| `focus:` | `activeFocus` | `focus-visible` semantics are out of scope for v1 |
| `disabled:` | `!enabled` | |
| `dark:` | active theme | see [theming.md](theming.md) |

Variants compose in any order and combine as AND:
`hover:pressed:bg-red-500` needs both. More-prefixed rules outrank
less-prefixed ones for the same property.

## How the handlers behave

When `hover:` or `pressed:` (without a native `pressed` property) is used,
Loom adds one invisible child item to the target holding a `HoverHandler` and
a passive `TapHandler`. It is stacked *below* every other child (`z: -1`), so:

- the target's own MouseAreas, handlers and interactive children keep every
  event — verified by test;
- `pressed:` on a bare Rectangle works because nothing else claims the press;
- `pressed:` does **not** fire on regions covered by a child that exclusively
  grabs the press (a MouseArea). Put the variant on the interactive element
  itself — its native `pressed` property is preferred automatically.

The watcher is visible to code that walks `children`; it has no size effects.

Targets created without a QML engine (plain C++ `new QQuickItem`) cannot get
handlers; a warning names the item.
