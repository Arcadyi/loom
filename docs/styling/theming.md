# Theming and accessibility

Loom separates fixed palette colours (`blue-500`) from semantic colours
(`surface`, `foreground`, `accent`). Build application UI from semantic names;
the active theme resolves them, and every typed binding and utility rule updates
live when that theme changes.

## Built-in semantic colours

| Token | Purpose |
| --- | --- |
| `background` | application background |
| `surface`, `surface-alt` | cards, panels, and secondary surfaces |
| `outline` | borders and dividers |
| `foreground`, `muted`, `faint` | primary through tertiary content |
| `accent`, `accent-hover`, `on-accent` | interactive emphasis and its content |
| `danger`, `on-danger`, `success`, `warning` | status and destructive actions |

Utility names stay dashed (`bg-surface-alt`); generated typed properties are
camel-cased (`Loom.color.surfaceAlt`). A design-defined name has no generated
property and is read from the group by key — `Loom.color["brand-500"]`, or
`Loom.color.brand500` through the camel alias. Both track a theme switch.

## Explicit and system theme modes

```qml
Loom.theme = "dark"                  // selects a theme and explicit mode
Loom.themeMode = Loom.SystemTheme    // follows the OS again
Loom.dark                            // metadata on the active theme
Loom.themes()                        // every known theme
```

```cpp
loom::setTheme("dark");
loom::setThemeMode(loom::ThemeMode::System);
```

With `"theme": { "default": "system" }`, Loom listens to
`Qt.styleHints.colorScheme`. `theme.light` and `theme.dark` map the two OS
schemes to any known themes, so the dark mapping can be `oled` rather than the
built-in `dark`. An explicit assignment to `Loom.theme` leaves system mode.

On a hot reload, an explicit active theme is preserved when it still exists.
System mode also stays system mode and re-resolves against the new mappings.

## Custom, fully themeable designs

```json
{
  "schemaVersion": 2,
  "tokens": {
    "colors": { "brand": "#7c5cff" },
    "radius": { "card": 14 },
    "durations": { "deliberate": 280 }
  },
  "themes": {
    "oled": {
      "extends": "dark",
      "dark": true,
      "tokens": {
        "colors": {
          "background": "#000000",
          "surface": "#0a0a0a",
          "accent": "brand"
        },
        "radius": { "card": 10 },
        "shadows": {
          "card": { "color": "#99000000", "offsetY": 8, "blur": 28 }
        },
        "durations": { "deliberate": 220 }
      }
    }
  },
  "theme": { "default": "system", "light": "light", "dark": "oled" }
}
```

`extends` snapshots any known base theme. Declaration order does not matter;
the loader resolves dependencies before applying them and rejects unknown or
cyclic bases. `dark` controls `Loom.dark` and the `dark:` variant.

A theme may override colour, spacing, text sizes, font weights and families,
tracking, radius, shadows, opacity, durations, and easings. Breakpoints and
container thresholds remain global because appearance should not silently
change layout. A theme colour may be a literal, a palette/design colour, or an
inherited semantic name. An unresolvable colour warns and leaves its inherited
value intact.

Besides `dark:`, every theme has a named variant:

```qml
Rectangle {
    Lo.style: "rounded-lg theme-dark:shadow-lg theme-dark:bg-black"
}
```

`theme-oled:` becomes available when the design above is loaded, just as
`theme-dark:` is available from the built-ins. Prefer semantic tokens for
ordinary theme differences. Named variants are for genuine one-off behavior
that is not a design-scale override.

## Motion preference

```qml
Loom.motionPreference = Loom.ReduceMotion
Loom.reduceMotion
```

The choices are `SystemMotion`, `ReduceMotion`, and `FullMotion`. Reduced motion
disables all Loom transitions and activates `motion-reduce:` rules. Qt 6.11 has
no portable OS reduced-motion style hint, so system mode currently reads
`LOOM_REDUCE_MOTION=1`; explicit QML or application settings override it.

```qml
Rectangle {
    Lo.style: "transition-all motion-reduce:transition-none"
}
```

## Contrast and direction

`Loom.highContrast` follows Qt's accessibility contrast preference and powers
the `high-contrast:` variant. `rtl:` and `ltr:` follow the application layout
direction. These are reactive just like theme and interaction states.

```qml
Rectangle {
    Lo.style: "border border-outline high-contrast:border-4 rtl:origin-right"
}
```

## Re-resolution and transitions

Theme switches re-resolve token identities rather than recompiling strings.
Only changed values are written. Add a transition utility where a visual should
ease instead of snap:

```qml
Rectangle {
    Lo.style: "bg-surface text-foreground transition-colors duration-200 ease-out"
}
```

The first application always snaps, and reduced motion always wins. See
[utilities.md](utilities.md#motion) for ownership and transition boundaries.
