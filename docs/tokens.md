# Typed tokens

All tokens hang off the `Loom` singleton (`import Loom`). Every group property
re-evaluates when the theme changes or a config loads.

## Colors — `Loom.color`

The full Tailwind palette: 22 hues (`slate gray zinc neutral stone red orange
amber yellow lime green emerald teal cyan sky blue indigo violet purple
fuchsia pink rose`) times 11 shades (`50…950`), camel-cased:
`Loom.color.blue500`, `Loom.color.slate50`. Plus `white`, `black`,
`transparent`, and the [semantic tokens](theming.md): `background surface
surfaceAlt outline foreground muted faint accent accentHover onAccent danger
onDanger success warning`.

Config-defined colors are reached with `Loom.color.value("brand-500")` (a
plain lookup — unlike the typed properties it does not re-evaluate on theme
switch).

## Spacing — `Loom.space`

Tailwind scale, key × 4 px: `s0 s0_5 s1 s1_5 s2 s2_5 s3 s3_5 s4 s5 … s12 s14
s16 s20 s24 … s96`. `Loom.space.s4` is 16. Halves use underscores (`s0_5` =
2). Runtime lookup: `Loom.space.value("18")`.

## Typography — `Loom.text`

- Sizes return a `{size, lineHeight}` value: `font.pixelSize:
  Loom.text.xl.size`. Names: `xs sm base lg xl x2l x3l x4l` (identifiers
  cannot start with a digit, so `2xl` is `x2l`; utility strings keep
  `text-2xl`). Runtime lookup: `Loom.text.value("2xl")`.
- Weights are ints for `font.weight`: `thin extralight light normal medium
  semibold bold extrabold black`.
- Tracking (letter spacing) in em: `trackingTighter trackingTight
  trackingNormal trackingWide trackingWider`.

## Radius — `Loom.radius`

`none sm base md lg xl x2l x3l full` (0 2 4 6 8 12 16 24 9999 px).

## Shadows — `Loom.shadow`

`none sm base md lg xl x2l` as `{color, offsetX, offsetY, blur, spread}`
values, derived from the Tailwind box shadows (single layer).

## Opacity — `Loom.opacity`

`o0 o5 o10 … o95 o100` mapping to 0.0–1.0.

## Motion — `Loom.duration`, `Loom.easing`

Durations in ms: `d75 d100 d150 d200 d300 d500 d700 d1000`. Easing curves as
`QEasingCurve` values (cubic beziers matching the CSS timing functions):
`linear easeIn easeOut easeInOut` — bind them from C++ animations; QML's
`easing` group is set by enum/points, not by curve object.

```qml
Behavior on opacity {
    NumberAnimation {
        duration: Loom.duration.d150
        easing.bezierCurve: [0.0, 0.0, 0.2, 1.0, 1.0, 1.0] // Loom easeOut
    }
}
```

## Breakpoints — `Loom.breakpoint`

The thresholds (px) behind the responsive variants: `sm 640, md 768, lg 1024,
xl 1280`. Exposed for custom logic; per-item responsiveness should use the
[`sm:`–`xl:` variants](responsive.md).

## Icons — `Loom.icon()`

`Loom.icon(source, color)` returns an image URL that serves `source` repainted
in `color`, for anything that takes one. Point `Loom.iconRoot` at your icon
directory once, then name assets bare:

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

`iconRoot` can also come from the JSON config, where it is relative to the
config file — see [configuration.md](configuration.md).

Setting `icon.color` cannot do this. Qt only tints an icon item that is a
*mask*, and a plain file source never is, so the color is accepted and then
ignored — see [limitations.md](limitations.md). Recolouring happens on the way
out of the image provider instead, which is why the same call works for
`Image.source` as well as a control's `icon.source`, with no delegate to
override.

Recolouring replaces color and keeps coverage, so anti-aliased edges stay soft
and fully transparent pixels stay transparent. A multi-color asset therefore
comes back monochrome; pass no color to serve it untouched.

Two things worth knowing:

- **A relative `source` resolves against `iconRoot`, not against the calling
  QML file.** A singleton cannot see which file called it — its context is the
  engine root — so per-file resolution is not something this can offer
  honestly. Pass `Qt.resolvedUrl("…")` to bypass the root for a one-off; any
  source with a scheme or a leading `/` is used exactly as given.
- **Set `iconRoot` before the UI loads.** `icon()` bindings track colors, not
  the root, so changing it later will not repaint icons already on screen.
- **Pass a token, not a literal, to stay themed.** `Loom.color.foreground`
  notifies, so the binding re-evaluates and the icon repaints on a theme
  switch. `"#16a34a"` is resolved once and stays put.

SVG sources are rasterised at the size the consumer asks for (`icon.width` /
`icon.height`, or `sourceSize`), so an icon stays crisp when scaled up rather
than being stretched from a default-size raster.
