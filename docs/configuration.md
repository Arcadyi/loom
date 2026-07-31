# Configuration

Loom's equivalent of `tailwind.config`: a JSON file of design tokens.

In a `loom` project, name it in the manifest and the rest is automatic:

```json
// loom.json
{ "design": "design/tokens.json", … }
```

`loom new` scaffolds exactly this. The file is then compiled into release
builds, **hot-reloaded by `loom dev`**, and loaded by `loom style` and
`loom lint` so project-defined classes are recognised. See
[manifest.md](manifest.md#design).

Outside a project, or to load an additional file, load it yourself:

```cpp
// C++, ideally before the engine loads (flicker-free):
loom::loadConfig(":/config/tokens.json");
```

```qml
// Or from QML at any point; everything re-resolves live:
Component.onCompleted: Loom.loadConfig(Qt.resolvedUrl("tokens.json"))
```

Returns false when the file cannot be read or parsed. Individually bad
entries warn (logging category `loom.config`) and are skipped.

## Loading versus reloading

`loadConfig()` **merges** into what is already defined, which is what you want
when layering a config over the built-in set.

`loom::reloadConfig()` **replaces**: every token resets to the built-in set
before the file is applied, so a token deleted from the file stops resolving.
This is what `loom dev` drives on each save, and it is why removing a colour
takes effect immediately rather than lingering until the next restart.

Either way a file that fails to parse changes nothing — the tokens that were
working stay live. Under `loom dev` that means a half-typed file mid-keystroke
costs you nothing.

On a reload the active theme is preserved when it still exists, and beats the
file's `defaultTheme`: if you switched to dark to look at it and then saved,
you meant to restyle dark, not to be thrown back to the default.

## Schema

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

- **colors** — nested hue objects become `hue-shade` keys (`brand-500`);
  flat entries keep their name. Everything merges into the built-in palette.
- **space** — extra spacing keys in pixels; usable everywhere `{n}` is
  (`p-18`, `w-18`, `gap-18`).
- **breakpoints** — moves the `sm/md/lg/xl` thresholds. The four tiers are
  structural; new tier names are rejected.
- **themes** — semantic-token overrides. Values are palette keys
  (config-defined included) or color literals. New themes copy from
  `extends` first (any already-known theme); the optional `"dark"` bool
  feeds `Loom.dark` and the `dark:` variant, and is inherited from the base
  otherwise. Merging into `light`/`dark` adjusts the built-ins in place.
- **defaultTheme** — switches the active theme after loading.
- **iconRoot** — directory that a relative [`Loom.icon()`](tokens.md#icons--loomicon)
  source resolves against, relative to this config file. Equivalent to setting
  `Loom.iconRoot` from QML; the config wins only if it loads later.

## Reach

Config-defined tokens work in utility strings immediately (`bg-brand-500`,
`w-18`) — the compile cache is invalidated on load, so strings compiled
before the config picked them up recompile correctly.

The typed surface is generated at build time, so config tokens are read with
the runtime lookups: `Loom.color.value("brand-500")`,
`Loom.space.value("18")`, `Loom.text.value("2xl")`. Note these are snapshot
reads — for theme-dependent custom colors, re-read on `Loom.theme` changes
or use a utility string.
