# Theming

Loom has two kinds of colour. **Palette** colours are fixed: `blue-500` is the
same blue in every theme. **Semantic** colours resolve through the active theme,
so `surface` is white in the light theme and `slate-900` in the dark one.

Build your UI from semantic colours and theming is free. Reach for a palette
colour when you mean *that colour specifically*, not "the surface colour".

## Semantic tokens

| Token | Light | Dark | Meaning |
| --- | --- | --- | --- |
| `background` | slate-50 | slate-950 | window background |
| `surface` | white | slate-900 | cards, panels, raised areas |
| `surface-alt` | slate-100 | slate-800 | hover states, wells, secondary surfaces |
| `outline` | slate-200 | slate-700 | borders, dividers |
| `foreground` | slate-900 | slate-50 | primary text |
| `muted` | slate-500 | slate-400 | secondary text |
| `faint` | slate-400 | slate-600 | disabled text, placeholders |
| `accent` | blue-600 | blue-500 | interactive elements |
| `accent-hover` | blue-700 | blue-400 | hovered accent |
| `on-accent` | white | white | text and icons on an accent fill |
| `danger` | red-600 | red-500 | destructive actions |
| `on-danger` | white | white | text on a danger fill |
| `success` | green-600 | green-500 | positive state |
| `warning` | amber-500 | amber-400 | cautionary state |

Note that `muted` and `faint` swap direction between themes: on a light
background, secondary text is *darker* than tertiary; on a dark background it is
lighter. That is intentional, and the reason both exist rather than one token
with an opacity modifier.

Utility strings use the dashed names — `bg-surface-alt`, `text-on-accent`. The
typed API camel-cases them — `Loom.color.surfaceAlt`, `Loom.color.onAccent` —
because QML identifiers cannot contain dashes.

## Switching at runtime

```qml
Loom.theme = "dark"      // from QML
Loom.dark                // bool, true while the active theme is dark
Loom.themes()            // ["dark", "light", ...] — every defined theme
```

```cpp
loom::setTheme("dark");  // from C++
QString current = loom::theme();
```

A switch re-evaluates every typed binding and re-applies every `Lo.style` that
resolves differently. Only properties whose values actually changed are written,
so a switch between two themes that agree on a colour costs nothing for the
items using it.

An unknown theme name warns and leaves the theme unchanged, rather than
resolving every semantic colour to nothing.

To animate the transition, put `transition-colors` on the items that should
ease rather than snap:

```qml
Rectangle {
    Lo.style: "bg-surface text-foreground transition-colors duration-200"
}
```

## The `dark:` variant

```qml
Rectangle { Lo.style: "bg-white dark:bg-slate-900 dark:hover:bg-slate-800" }
```

`dark:` is a [state variant](states.md) and composes with every other one.

Prefer a semantic token where one exists. `bg-surface` is one class that is
already correct in both themes, and stays correct in a third theme you add
later; `bg-white dark:bg-slate-900` is two classes that hard-code the two themes
you have today. Use `dark:` for genuine one-off divergences — a shadow that
needs to be heavier on dark, an image that needs a different tint.

## Custom themes

Themes are defined in the [design token file](configuration.md). A theme
extends an existing one and overrides the semantic tokens it cares about:

```json
{
  "themes": {
    "oled": {
      "extends": "dark",
      "background": "#000000",
      "surface": "#0a0a0a",
      "surface-alt": "surface"
    }
  },
  "defaultTheme": "oled"
}
```

A value can be:

- a **hex literal** — `"#000000"`, `"#ff00aa80"` with alpha;
- a **palette key** — `"blue-600"`, `"slate-950"`;
- a **colour your config defines** in its own `colors` block;
- a **semantic name the theme already has**, inherited from the theme it
  extends. `"surface-alt": "surface"` above means "the same as whatever surface
  resolves to in the base theme".

That last form does not see names defined in the *same* object, because the
resolution order would depend on hash iteration order and would not be
reproducible. Alias from the base theme, or write the value out.

A value that resolves to none of these warns and is skipped, leaving the
inherited value in place. It used to produce an invalid colour silently.

### `extends` is a snapshot

Extending copies the base theme's colours at the moment the derived theme is
defined. Later edits to the base — in the same file or a later config load — do
not propagate to themes that already extended it. Reloading the whole design
file re-defines everything from scratch, so under `loom dev` this is invisible;
it matters only if you build themes programmatically.

Themes may extend in any order within one file: the loader retries until it
makes no progress, then reports whatever is left as extending an unknown theme.

### Setting the default

`defaultTheme` names the theme a fresh process starts in. On a **reload** under
`loom dev`, the theme you are currently looking at wins instead — someone who
switched to dark to inspect it and then saved the design file meant to restyle
dark, not to be thrown back to the default. `defaultTheme` reapplies only when
the theme you were on no longer exists.

## What a theme can and cannot change

A theme is **colours only**. `struct Theme` holds a map of semantic names to
colours, and nothing else.

That means these are *not* themeable today: radius, the type scale, shadows
(including their alpha, which a dark theme genuinely wants different), spacing,
durations, easings, and the font. A brand with sharp corners changes `rounded-*`
everywhere it appears rather than defining a theme; a design token file can move
those scales globally, but not per theme.

Making the whole token set themeable is on the roadmap. Until then, the
workaround for a value that has to differ per theme is the `dark:` variant,
which reaches every scale rather than only colours:

```qml
Rectangle {
    // A shadow that reads as too weak on a dark background.
    Lo.style: "bg-surface rounded-lg shadow-md dark:shadow-2xl"
}
```

That covers light-versus-dark. For a *third* theme that needs its own radius,
there is no variant to reach for — bind the string instead:

```qml
Rectangle {
    readonly property bool sharp: Loom.theme === "brand"
    Lo.style: "bg-surface " + (sharp ? "rounded-none" : "rounded-lg")
}
```

Keep the theme *name* out of the `Lo.style` binding, as above. The class checker
reads every string literal in the binding, so an inline `Loom.theme === "brand"`
would make it report `brand` as an unknown class — see
[what the checker can and cannot see](../tooling/cli.md).

## Following the system theme

Loom does **not** follow the operating system's light/dark preference. There is
no `"system"` theme mode, and `Loom.dark` reflects only which theme you last
set.

Wire it up yourself with Qt's own signal:

```qml
import QtQuick

Item {
    Component.onCompleted: syncTheme()

    Connections {
        target: Qt.styleHints
        function onColorSchemeChanged() { syncTheme() }
    }

    function syncTheme() {
        Loom.theme = Qt.styleHints.colorScheme === Qt.Dark ? "dark" : "light"
    }
}
```

`Qt.styleHints.colorScheme` needs Qt 6.5 or newer, which loom's minimum of 6.11
already guarantees.

## Reading the active theme

```qml
Loom.theme          // "light", "dark", or a custom name
Loom.dark           // bool
Loom.themes()       // every defined theme name, sorted
Loom.color.surface  // resolves through the active theme, and re-evaluates on switch
```

`Loom.color.value("brand-500")` is a plain lookup for config-defined names. It
returns the value at the moment you call it and does **not** re-evaluate on a
theme switch, unlike the typed properties. Use it for one-off reads, not for
bindings that need to stay correct.
