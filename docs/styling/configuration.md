# The design token file

Loom's equivalent of `tailwind.config`: a JSON file of design tokens that
extends the built-in set.

In a `loom` project, name it in the manifest and the rest is automatic:

```json
// loom.json
{ "design": "design/tokens.json", … }
```

`loom new` scaffolds exactly this. The file is then compiled into release
builds, **hot-reloaded by `loom dev`**, and loaded by `loom style` and
`loom lint` so project-defined classes are recognised as valid rather than
reported as typos. See [../tooling/manifest.md](../tooling/manifest.md#design).

## A first file

The one `loom new` generates, which is a complete and useful example:

```json
{
  "colors": {
    "brand": {
      "400": "#8b7cff",
      "500": "#6d5cff",
      "600": "#5a49e0"
    }
  },
  "themes": {
    "light": { "accent": "brand-500" },
    "dark":  { "accent": "brand-400" }
  }
}
```

Three lines of consequence: `bg-brand-500` and `Loom.color.value("brand-500")`
now resolve, and every existing `bg-accent` in the project is now the brand
colour, correctly lighter on dark backgrounds. Nothing else in the UI had to
change.

## Loading it yourself

Outside a project, or to layer an additional file:

```cpp
// C++, ideally before the engine loads, for a flicker-free first frame:
loom::loadConfig(":/config/tokens.json");
```

```qml
// Or from QML at any point; everything re-resolves live:
Component.onCompleted: Loom.loadConfig(Qt.resolvedUrl("tokens.json"))
```

Both accept `file:` and `qrc:` paths. Both return `false` when the file cannot
be read or parsed. Individually bad entries warn on the `loom.config` logging
category and are skipped, leaving the rest of the file applied.

## Loading versus reloading

| | `loadConfig()` | `reloadConfig()` |
| --- | --- | --- |
| Existing tokens | merged into | **reset to built-in first** |
| A token the file no longer defines | survives | stops resolving |
| Use for | layering at startup | `loom dev`, on every save |

Replace-don't-merge is what makes deleting a colour from the file take effect
immediately rather than lingering until the next restart. Every key follows it,
including `iconRoot`: dropping the key clears the root rather than leaving the
old one live.

Either way, **a file that fails to parse changes nothing** — the tokens that
were working stay live. Under `loom dev` that matters more than it sounds,
because a file is malformed for most of the time someone is typing in it.

On a reload the **active theme is preserved** when it still exists, and beats
the file's `defaultTheme`. If you switched to dark to look at it and then saved,
you meant to restyle dark, not to be thrown back to the default.

## Schema

The full vocabulary. A JSON Schema is installed at
`share/loom/schemas/design-v1.schema.json`; point your editor at it for
completion and validation.

```json
{
  "colors": {
    "brand": { "500": "#7c5cff", "600": "#6a4be0" },
    "highlight": "#ffcc00"
  },
  "space": { "18": 72 },
  "breakpoints": { "md": 800 },
  "themes": {
    "light": { "surface": "#fcfcfc" },
    "oled":  { "extends": "dark", "surface": "#000000", "dark": true }
  },
  "defaultTheme": "oled",
  "iconRoot": "assets/icons"
}
```

### `colors`

Nested hue objects become `hue-shade` keys — `brand` with a `500` becomes
`brand-500`. Flat entries keep their name, so `"highlight": "#ffcc00"` becomes
the class `bg-highlight`.

Everything merges into the built-in palette. You can override a built-in name;
`"blue": { "500": "#0000ff" }` changes `bg-blue-500` everywhere.

Values are colour literals: `#rgb`, `#rrggbb`, `#aarrggbb`, or any name
`QColor::fromString` accepts.

### `space`

Extra spacing steps, in pixels, usable everywhere `{n}` is: `p-18`, `w-18`,
`gap-18`, `mt-18`.

```json
{ "space": { "18": 72, "128": 512 } }
```

Keys are strings and need not be numeric — `"gutter": 20` gives you `p-gutter` —
though staying numeric keeps the scale legible.

### `breakpoints`

Moves the `sm`/`md`/`lg`/`xl` thresholds. Two rules the loader enforces:

- a threshold must be **greater than zero**; one at or below zero is met by
  every window, which makes the tier meaningless, and is rejected;
- thresholds should be **strictly widening**. A tier no wider than the one
  before it warns, because its classes can never be the widest match and will
  appear to do nothing.

The four tiers are structural — they map to the four prefixes — so a config can
move them but cannot add a fifth or rename one. An unknown key is rejected with
a warning.

### `themes`

Semantic-token overrides. Merging into `light` or `dark` adjusts the built-ins
in place; any other name defines a new theme.

```json
{
  "themes": {
    "oled": {
      "extends": "dark",
      "background": "#000000",
      "surface": "#0a0a0a",
      "surface-alt": "surface",
      "dark": true
    }
  }
}
```

- **`extends`** copies from any already-known theme before applying the
  overrides. Themes may extend in any order within one file: the loader retries
  until it makes no progress, then reports whatever is left as extending an
  unknown theme.
- **`dark`** feeds `Loom.dark` and the `dark:` variant. Inherited from the base
  theme when omitted.
- **Every other key** is a semantic token name mapped to a value.

A value can be a colour literal, a palette key (config-defined included), or
**a semantic name the theme already has** — inherited from the theme it extends.
`"surface-alt": "surface"` above means "the same as whatever `surface` resolves
to in the base". Aliases do not see names defined in the *same* object, because
the resolution order would depend on hash iteration and would not be
reproducible.

A value that resolves to none of these warns and is skipped, leaving the
inherited value in place.

Full detail in [theming.md](theming.md#custom-themes).

### `defaultTheme`

The theme a fresh process starts in. On a reload, the currently active theme
wins unless it no longer exists.

### `iconRoot`

The directory a relative [`Loom.icon()`](tokens.md#icons--loomicon) source
resolves against, **relative to this file**. Equivalent to setting
`Loom.iconRoot` from QML; whichever happens later wins.

Under `loom dev` the path is resolved against the file's location in your
project, not against wherever the runtime staged the document — so a relative
root keeps working across a hot reload.

## What cannot be configured

Stated plainly, because the omissions are not obvious from the schema. These
scales are **fixed**:

- radius, text sizes and their line heights, font weights, tracking;
- shadows — including their alpha, which a dark theme genuinely wants different;
- opacity steps, durations, easings;
- the font family. There is no font token in loom at all.

A brand that wants sharp corners changes `rounded-*` at every call site rather
than redefining the scale. Widening the configurable surface is on the roadmap;
until then, [`theming.md`](theming.md#what-a-theme-can-and-cannot-change) covers
the workarounds.

Themes are likewise **colours only**. There is no themed radius, spacing,
typography or motion.

## Reach

Config-defined tokens work in utility strings immediately — `bg-brand-500`,
`w-18`. The compile cache is invalidated on load, so strings compiled before the
config defined a name recompile rather than keeping the gap.

The typed `Loom.*` surface is generated at build time, so config tokens are read
with the runtime lookups instead:

```qml
Loom.color.value("brand-500")
Loom.space.value("18")
```

Those are **snapshot** reads: they do not re-evaluate on a theme switch, unlike
the generated properties. For a theme-dependent custom colour, prefer a utility
string, which re-resolves on every apply.

## Checking it

`loom style --check` and `loom lint` load the file named by `loom.json`'s
`design` key before checking, so a class naming a config-defined token is
recognised rather than reported as a typo.

That is the *only* file they load. A config your application loads at runtime
with `loadConfig()` — a second file layered in C++, a theme fetched from
somewhere — is invisible to the checker, and classes naming its tokens will be
reported as unknown. Keep the project's tokens in the file `loom.json` names,
and the checker and the application stay in agreement by construction.

`loom style --catalogue` dumps the whole vocabulary including config-defined
names. Both the token set and the active theme affect the output, so dump it
under the same config your application uses.
