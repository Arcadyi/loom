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
  "$schema": "https://raw.githubusercontent.com/Arcadyi/loom/master/schemas/design-v2.schema.json",
  "schemaVersion": 2,
  "tokens": {
    "colors": {
      "brand": {
        "400": "#8b7cff",
        "500": "#6d5cff",
        "600": "#5a49e0"
      }
    }
  },
  "themes": {
    "light": { "tokens": { "colors": { "accent": "brand-500" } } },
    "dark":  { "tokens": { "colors": { "accent": "brand-400" } } }
  },
  "theme": { "default": "system", "light": "light", "dark": "dark" },
  "styles": { "card": "bg-surface border border-outline rounded-lg shadow-sm" },
  "lint": { "arbitraryValues": "warn" }
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
be read, parsed, or validated. Schema v2 validation is transactional: an
unknown field, invalid token value, theme cycle, or invalid policy leaves the
complete previous design live. A colour reference that is structurally valid
but cannot resolve is warned and skipped, leaving the inherited colour.

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
`theme.default`. If you switched to dark to look at it and then saved,
you meant to restyle dark, not to be thrown back to the default.

## Schema

The full vocabulary. A JSON Schema is installed at
`share/loom/schemas/design-v2.schema.json`; point your editor at it for
completion and validation.

```json
{
  "schemaVersion": 2,
  "tokens": {
    "colors": {
      "brand": { "500": "#7c5cff", "600": "#6a4be0" },
      "highlight": "#ffcc00"
    },
    "space": { "18": 72 },
    "textSizes": { "display": { "size": 48, "lineHeight": 52 } },
    "fontWeights": { "book": 450 },
    "fontFamilies": { "brand": ["Inter", "Sans Serif"] },
    "tracking": { "brand": -0.015 },
    "radius": { "card": 18 },
    "shadows": { "floating": { "color": "#40000000", "offsetY": 8, "blur": 24 } },
    "opacity": { "subtle": 0.72 },
    "durations": { "deliberate": 280 },
    "easings": { "springy": [0.2, 0.8, 0.2, 1] },
    "breakpoints": { "2xl": 1536 },
    "containers": { "content": 720 }
  },
  "themes": {
    "light": { "tokens": { "colors": { "surface": "#fcfcfc" } } },
    "oled":  {
      "extends": "dark",
      "dark": true,
      "tokens": { "colors": { "surface": "#000000" }, "radius": { "card": 12 } }
    }
  },
  "theme": { "default": "oled", "light": "light", "dark": "oled" },
  "styles": { "card": "bg-surface rounded-card p-4" },
  "lint": { "arbitraryValues": "deny" },
  "iconRoot": "assets/icons"
}
```

Every scale lives below `tokens`. The available families are `colors`, `space`,
`textSizes`, `fontWeights`, `fontFamilies`, `tracking`, `radius`, `shadows`,
`opacity`, `durations`, `easings`, `breakpoints`, and `containers`. Themes can
override every visual/motion family, but not breakpoints or containers because
changing those with appearance would also change layout.

### `tokens.colors`

Nested hue objects become `hue-shade` keys — `brand` with a `500` becomes
`brand-500`. Flat entries keep their name, so `"highlight": "#ffcc00"` becomes
the class `bg-highlight`.

Everything merges into the built-in palette. You can override a built-in name;
`"blue": { "500": "#0000ff" }` changes `bg-blue-500` everywhere.

Values are colour literals: `#rgb`, `#rrggbb`, `#aarrggbb`, or any name
`QColor::fromString` accepts.

### `tokens.space`

Extra spacing steps, in pixels, usable everywhere `{n}` is: `p-18`, `w-18`,
`gap-18`, `mt-18`.

```json
{ "schemaVersion": 2, "tokens": { "space": { "18": 72, "128": 512 } } }
```

Keys are strings and need not be numeric — `"gutter": 20` gives you `p-gutter` —
though staying numeric keeps the scale legible.

### `tokens.breakpoints` and `tokens.containers`

Moves the `sm` through `2xl` thresholds. Two rules the loader enforces:

- a threshold must be **greater than zero**; one at or below zero is met by
  every window, which makes the query meaningless, and is rejected;
- conventional thresholds should be **strictly widening**. A threshold no wider
  than the one before it warns, because its classes can never be the narrowest
  matching min-width condition and will appear to do nothing.

Breakpoint and container names are dynamic. Built-ins include `sm` through
`2xl` and `3xs` through `7xl`; a design can add or replace any positive-pixel
name, which immediately appears in the compiler, catalogue, lint, and LSP.

### `themes`

Semantic-token overrides. Merging into `light` or `dark` adjusts the built-ins
in place; any other name defines a new theme.

```json
{
  "themes": {
    "oled": {
      "extends": "dark",
      "dark": true,
      "tokens": { "colors": {
        "background": "#000000",
        "surface": "#0a0a0a",
        "surface-alt": "surface"
      } }
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
- **`tokens`** contains the same visual scales as the root token object.

A value can be a colour literal, a palette key (config-defined included), or
**a semantic name the theme already has** — inherited from the theme it extends.
`"surface-alt": "surface"` above means "the same as whatever `surface` resolves
to in the base". Aliases do not see names defined in the *same* object, because
the resolution order would depend on hash iteration and would not be
reproducible.

A value that resolves to none of these warns and is skipped, leaving the
inherited value in place.

### Recipes and arbitrary-value policy

Each `styles` entry is a named recipe. Invoke it with `@name`; recipes may use
variants and other recipes, and call-site variants compose with their contents:

```text
Rectangle { Lo.style: "@card hover:@card" }
```

Cycles, missing recipes, nesting beyond 32, and expansion beyond 4096 rules are
rejected. `lint.arbitraryValues` is `allow`, `warn` (the default), or `deny`.
Runtime accepts typed arbitrary values in every mode; lint and LSP enforce the
design-system policy.

Full theme detail is in [theming.md](theming.md#custom-themes).

### `states`

Conditions your application owns, made available as variant prefixes. Each
value is a description, shown in editor hovers.

```json
"states": {
  "syncing": "Has local changes not yet saved to the server",
  "stale": "Backing data is older than the refresh interval"
}
```

`syncing:border-warning`, `not-syncing:opacity-60` and `group-syncing/row:` all
work from there, ranking exactly like the built-in state variants. Supply the
values with `Lo.states` or a bool property of the same name — see
[states.md](states.md#states-your-application-owns).

They are declared here rather than invented at the call site because the style
compiler caches by exact class string, process-wide, so a variant name has to
resolve when the string is parsed. Putting them in the design file is also what
makes them free for tooling: `loom style`, `loom lint` and `loom lsp` read the
same registry the runtime does, so completion, typo suggestions and hovers work
on your states with nothing else to configure.

At most 32 per project. A name that collides with a built-in variant is
rejected rather than silently shadowed, as are the `not-`, `group-`, `theme-`,
`min-` and `max-` prefixes, which already spell composed variants. Names are
lower-case letters, digits and dashes, starting with a letter.

**Version note:** a design file using `states` is rejected outright by loom
0.4 and earlier, which refuse unknown top-level keys.

### `theme`

`theme.default` is a theme name or `"system"`. System mode listens to the
operating-system colour scheme and maps it through `theme.light` and
`theme.dark`, which may name custom themes. Assigning `Loom.theme` switches
back to explicit mode; assigning `Loom.themeMode = Loom.SystemTheme` resumes
following the OS. On a reload, the current explicit theme wins when it still
exists, while system mode remains system mode.

### `iconRoot`

The directory a relative [`Loom.icon()`](tokens.md#icons--loomicon) source
resolves against, **relative to this file**. Equivalent to setting
`Loom.iconRoot` from QML; whichever happens later wins.

Under `loom dev` the path is resolved against the file's location in your
project, not against wherever the runtime staged the document — so a relative
root keeps working across a hot reload.

## What cannot be configured

Every visual and motion scale is configurable and themeable: colour, spacing,
typography, font family and weight, tracking, radius, shadow, opacity, duration,
and easing. Breakpoints and containers are global by design; making them change
with appearance would also change layout during a theme switch. Structural
behavior and utility parsing are framework features rather than design tokens.

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

`loom style --catalogue --design design/tokens.json` dumps the whole vocabulary
including config-defined names from every theme, so `theme-name:` classes are
available before that theme is active. Load the same design your application
uses.
