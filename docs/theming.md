# Theming

## Semantic tokens

Palette colors (`blue-500`) never change. Semantic tokens resolve through the
active theme:

| Token | Light | Dark | Meaning |
| --- | --- | --- | --- |
| `background` | slate-50 | slate-950 | window background |
| `surface` | white | slate-900 | cards, panels |
| `surface-alt` | slate-100 | slate-800 | hover states, wells |
| `outline` | slate-200 | slate-700 | borders, dividers |
| `foreground` | slate-900 | slate-50 | primary text |
| `muted` | slate-500 | slate-400 | secondary text |
| `faint` | slate-400 | slate-600 | disabled text, placeholders |
| `accent` | blue-600 | blue-500 | interactive elements |
| `accent-hover` | blue-700 | blue-400 | hovered accent |
| `on-accent` | white | white | text on accent |
| `danger` / `on-danger` | red-600 / white | red-500 / white | destructive |
| `success` | green-600 | green-500 | positive |
| `warning` | amber-500 | amber-400 | cautionary |

Typed access camel-cases the keys: `Loom.color.surfaceAlt`,
`Loom.color.onAccent`. Utility strings keep the dashes: `bg-surface-alt`,
`text-on-accent`.

## Switching at runtime

```qml
Loom.theme = "dark"          // or from C++: loom::setTheme("dark")
Loom.dark                    // bool, true while the active theme is dark
Loom.themes()                // ["dark", "light", ...]
```

A switch re-evaluates every typed binding and re-applies every `Lo.style`
that resolves differently — only changed properties are written.

## The `dark:` variant

```qml
Rectangle { Lo.style: "bg-white dark:bg-slate-900 dark:hover:bg-slate-800" }
```

`dark:` composes with every other variant. Prefer semantic tokens
(`bg-surface`) where one exists; use `dark:` for one-off divergences.

## Custom themes

Defined via the JSON config — see [configuration.md](configuration.md).
Themes extend an existing theme and override the semantic tokens they care
about:

```json
{
  "themes": { "oled": { "extends": "dark", "surface": "#000000" } },
  "defaultTheme": "oled"
}
```
