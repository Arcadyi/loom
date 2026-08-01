# Typed tokens — the `Loom` singleton

Every token is reachable as a typed property on the `Loom` singleton:

```qml
import QtQuick
import Loom

Rectangle {
    color: Loom.color.blue500
    radius: Loom.radius.lg
    border.width: 1
    border.color: Loom.color.outline
}
```

These are real QML properties with notify signals, generated into
`loom.qmltypes`. That means autocompletion in Qt Creator and VS Code, qmllint
catching `Loom.color.blue5000` as an error, and bindings that re-evaluate
automatically when the theme changes or a design file reloads.

This document is the **identifier** reference: what each group is called and
what type it returns. For the numeric values behind the keys — what `space.s7`
actually is in pixels — see the [scales in
utilities.md](utilities.md#scales), which both layers resolve from.

## Typed properties versus utility strings

Both read the same registry, so they never disagree. Choose per site:

| Use a typed token when | Use a utility string when |
| --- | --- |
| you need the value in an expression: `width: Loom.space.s4 * 3` | you are setting a property the utility already covers |
| the property has no utility: `Behavior`, `Gradient`, custom C++ | you want variants — `hover:`, `md:`, `dark:` |
| you want qmllint to check the name | you want the whole appearance on one line |
| you are writing C++ | |

Mixing them on one item is fine. Mixing them on one *property* is not — see
[the conflict rules](utilities.md#conflict-rules--lostyle-versus-your-own-code).

## Naming

Utility strings use the names as written in the design vocabulary
(`surface-alt`, `blue-500`, `2xl`). QML identifiers cannot contain dashes or
start with a digit, so the typed API transforms them:

| Vocabulary | Typed | Rule |
| --- | --- | --- |
| `surface-alt` | `Loom.color.surfaceAlt` | dashes become camelCase |
| `blue-500` | `Loom.color.blue500` | dash dropped between hue and shade |
| `2xl` | `Loom.text.x2l`, `Loom.radius.x2l` | leading digit gets an `x` prefix |
| `0.5` | `Loom.space.s0_5` | numeric keys get an `s` prefix; `.` becomes `_` |
| `75` (ms) | `Loom.duration.d75` | durations get a `d` prefix |
| `50` (%) | `Loom.opacity.o50` | opacities get an `o` prefix |
| `in-out` | `Loom.easing.easeInOut` | |

The transformation is mechanical, and every generated name is in
`loom.qmltypes`, so autocompletion is the fastest way to find one.

## Groups

### `Loom.color` → `color`

The full Tailwind palette — 22 hues × 11 shades — plus `white`, `black`,
`transparent`, and the 14 [semantic tokens](theming.md).

```qml
Loom.color.blue500      // palette; identical in every theme
Loom.color.slate50
Loom.color.surface      // semantic; resolves through the active theme
Loom.color.onAccent
```

Semantic properties re-evaluate on a theme switch, so a binding on one stays
correct without any work on your part.

### `Loom.space` → `qreal`

`Loom.space.s0`, `s0_5`, `s1`, `s1_5`, `s2`, `s2_5`, `s3`, `s3_5`, `s4`, `s5` …
`s12`, `s14`, `s16`, `s20`, `s24`, `s28`, `s32`, `s36`, `s40`, `s44`, `s48`,
`s52`, `s56`, `s60`, `s64`, `s72`, `s80`, `s96`. 34 steps; the number is the key
× 4 px, so `Loom.space.s4` is 16.

### `Loom.text`

Three sub-families in one group.

**Sizes** return a `LoomTextStyle` value with `size` and `lineHeight`:

```qml
Text {
    font.pixelSize: Loom.text.xl.size
    lineHeight: Loom.text.xl.lineHeight
    lineHeightMode: Text.FixedHeight
}
```

Names: `xs sm base lg xl x2l x3l x4l`.

**Weights** are ints for `font.weight`: `Loom.text.thin` … `Loom.text.black`
(`thin extralight light normal medium semibold bold extrabold black`).

**Tracking** is in em, for `font.letterSpacing` after multiplying by the pixel
size: `trackingTighter trackingTight trackingNormal trackingWide trackingWider`.

### `Loom.font` → `QStringList`

Font-family fallback lists: `sans`, `serif`, and `mono`. Design-defined families
are available through `Loom.font.value("brand")`; utility strings use
`font-brand`. Qt Quick's `font.family` accepts one family, so the utility selects
the first available name while the typed token preserves the full fallback list.

### `Loom.radius` → `qreal`

`none sm base md lg xl x2l x3l full`.

### `Loom.shadow` → `LoomShadow`

A value with `color`, `offsetX`, `offsetY`, `blur` and `spread`. Names:
`none sm base md lg xl x2l`.

```qml
import QtQuick.Effects

RectangularShadow {
    anchors.fill: card
    color: Loom.shadow.lg.color
    blur: Loom.shadow.lg.blur
    spread: Loom.shadow.lg.spread
    offset: Qt.vector2d(Loom.shadow.lg.offsetX, Loom.shadow.lg.offsetY)
}
```

Usually you want the `shadow-lg` utility instead, which builds and manages this
item for you.

### `Loom.opacity` → `qreal`

`o0 o5 o10 … o95 o100`, mapping to 0.0–1.0.

### `Loom.duration` → `int`

Milliseconds: `d75 d100 d150 d200 d300 d500 d700 d1000`.

### `Loom.easing` → `QEasingCurve`

`linear easeIn easeOut easeInOut`.

These are curve *objects*, which QML's `easing` grouped property does not accept
— it is configured by enum or by control points. So bind them from C++, or use
the control points directly in QML:

```qml
Behavior on opacity {
    NumberAnimation {
        duration: Loom.duration.d150
        easing.bezierCurve: [0.0, 0.0, 0.2, 1.0, 1.0, 1.0]   // Loom easeOut
    }
}
```

The four curves' control points are listed in
[utilities.md](utilities.md#easing).

### `Loom.breakpoint` → `int`

`sm md lg xl x2l`, the pixel thresholds behind the responsive variants. Exposed for
layout logic that needs the number:

```qml
GridLayout {
    columns: width >= Loom.breakpoint.lg ? 3 : 1
}
```

For per-item responsiveness prefer the [`sm:`–`2xl:`
variants](responsive.md), which track the item's own window.

Config-defined breakpoint names do not acquire generated QML properties; query
them with `Loom.breakpoint.value("compact")`. Container thresholds are consumed
by `@compact:` and `@compact/name:` variants and have no typed singleton group.

## Theme and accessibility state

The singleton also exposes reactive application state:

```qml
Loom.themeMode = Loom.SystemTheme
Loom.motionPreference = Loom.SystemMotion

Loom.theme         // resolved theme name
Loom.dark          // whether that theme is dark
Loom.highContrast  // system accessibility preference
Loom.reduceMotion  // resolved system/override motion preference
```

Explicit alternatives are `Loom.ExplicitTheme`, `Loom.ReduceMotion`, and
`Loom.FullMotion`. See [theming.md](theming.md) for system theme mappings and
motion variants.

## Runtime lookup for design-defined keys

Token groups have a `value(key)` method taking the *vocabulary* name, for keys
that have no generated property — anything a design token file added:

```qml
Rectangle {
    color: Loom.color.value("brand-500")
    width: Loom.space.value("18")
    radius: Loom.radius.value("card")
}
```

The same lookup is available on `font`, `shadow`, `opacity`, `duration`,
`easing`, and `breakpoint`. Because `Loom.text` combines three scales,
`value(key)` reads a text size while `weight(key)` and `tracking(key)` read the
other two.

Two things to know:

- **`value()` is a snapshot.** It returns what the key resolves to at the moment
  of the call and does **not** re-evaluate on a theme switch or a design reload,
  unlike the typed properties. For a binding that must stay correct across a
  theme change, use a typed property, or use a utility string, which re-resolves
  on every apply.
- An unknown key returns a default-constructed value — an invalid `QColor`, a
  zero `qreal` — rather than raising. Check with `loom lint` instead.

## Icons — `Loom.icon()`

`Loom.icon(source, color)` returns an image URL that serves `source` repainted
in `color`, usable anywhere a URL is:

```qml
// once, before the UI loads
Loom.iconRoot = Qt.resolvedUrl("assets/icons")

Button {
    Lo.style: "bg-surface rounded-full"
    display: AbstractButton.IconOnly
    icon.width: 20
    icon.height: 20
    icon.source: Loom.icon("home.svg", Loom.color.foreground)
}
```

`iconRoot` can also come from the [design token file](configuration.md), where
it is resolved relative to that file.

### Why this exists

Setting `icon.color` cannot do it. Qt tints an icon item only while it is a
*mask*, and a plain `.svg` or `.png` source never is — so the colour is accepted
and then silently dropped, as is `palette.buttonText`, which feeds the same
path. Recolouring happens on the way out of an image provider instead, which is
why the same call works for `Image.source` as for a control's `icon.source`,
with no delegate to override.

It also fixes a second problem: Qt's SVG renderer does not implement
`currentColor` and resolves it to **black**, so icon sets that stroke with it —
Lucide, Feather, Bootstrap Icons — render black until something recolours them.

### Behaviour worth knowing

- **Recolouring replaces colour and keeps coverage.** Anti-aliased edges stay
  soft, fully transparent pixels stay transparent, and a multi-colour asset
  comes back monochrome. Pass no colour to serve the asset untouched.
- **A relative `source` resolves against `iconRoot`, not against the calling QML
  file.** A singleton cannot see which file called it — its context is the
  engine root — so per-file resolution is not something this can offer honestly.
  Pass `Qt.resolvedUrl("…")` for a one-off; any source with a scheme or a
  leading `/` is used exactly as given.
- **Set `iconRoot` before the UI loads.** `icon()` bindings track the colour,
  not the root, so changing it later will not repaint icons already on screen.
- **Pass a token, not a literal, to stay themed.** `Loom.color.foreground`
  notifies, so the binding re-evaluates and the icon repaints on a theme switch.
  `"#16a34a"` is resolved once and stays put.
- **SVGs are rasterised at the size the consumer asks for** — `icon.width` /
  `icon.height`, or `sourceSize` — so an icon stays crisp when scaled up rather
  than being stretched from a default-size raster.

## From C++

The same registry backs a small C++ surface:

```cpp
#include <loom/loom.h>

loom::setTheme("dark");
QString active = loom::theme();
loom::loadConfig("design/tokens.json");
```

There is no C++ accessor for individual token values today — the typed groups
are QML-only. See [../reference/cpp-api.md](../reference/cpp-api.md) for the
full public surface.

## Theme reactivity, precisely

| Access | Re-evaluates on theme switch | Re-evaluates on design reload |
| --- | --- | --- |
| `Loom.color.surface` and every typed property | yes | yes |
| `Lo.style: "bg-surface"` | yes | yes, including recompiling for new token names |
| `Loom.color.value("brand")` | **no** | **no** |

The last row is the one that catches people. A `value()` call in a binding looks
reactive and is not.
