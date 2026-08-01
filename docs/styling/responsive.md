# Responsive breakpoints

Tailwind's mobile-first model. An unprefixed utility always applies; a prefixed
one applies at that width **and up**.

```qml
Rectangle {
    // 64 px wide by default, 160 from sm up, 256 from lg up.
    Lo.style: "w-16 sm:w-40 lg:w-64 bg-surface md:shadow-md"
}
```

| Prefix | Minimum window width |
| --- | --- |
| *(none)* | always |
| `sm:` | 640 px |
| `md:` | 768 px |
| `lg:` | 1024 px |
| `xl:` | 1280 px |
| `2xl:` | 1536 px |

Because the prefixes are min-width and cumulative, an item at 1600 px matches
`sm:`, `md:`, `lg:`, `xl:`, **and** `2xl:` — so write the base case unprefixed and layer
overrides upward, exactly as in Tailwind.

Every named breakpoint also has `max-{name}:`, whose upper bound is one pixel
below the threshold. Typed arbitrary viewport queries use `min-[900]:` and
`max-[1199]:`. Names and thresholds come from the live design registry, so a
custom `tablet` token immediately enables `tablet:` and `max-tablet:`.

## What drives viewport matching

**The width of the window the item lives in** — not the screen, not the parent
item, not the application. Specifically:

- the item's `window` property, tracked through reparenting and through the item
  being moved between windows;
- an item that is in no window at all styles at the base tier, which is what a
  freshly created item does before it is parented into a scene;
- each item resolves its own tier, so two windows of different widths style
  independently and correctly.

That last point is why there is deliberately **no global "current breakpoint"
property**. A singleton value would be wrong in any application with more than
one window, and quietly wrong at that.

For component-local responsiveness, mark an ancestor and use a container query:

```qml
Item {
    Lo.container: true
    Lo.containerName: "card"
    Rectangle { Lo.style: "p-2 @md/card:p-6 @max-sm/card:text-sm" }
}
```

`@md:` and `@max-md:` use the `tokens.containers` scale; `@min-[500]:` and
`@max-[799]:` are the typed arbitrary forms. Loom searches the nearest matching
`Lo.container` ancestor and observes only that item's width.

## How matching is computed

Each rule stores exact minimum and maximum pixel bounds, so dynamic names,
max-width queries, and arbitrary ranges do not depend on a fixed tier count.

The conventional `sm` through `2xl` names are expected to widen in that order;
the loader warns when an override breaks that convention.

## Combining with state variants

Breakpoints and states are independent axes, and **states outrank breakpoints**:

```qml
// Red from md up; black while hovered, at any width.
Lo.style: "bg-white hover:bg-black md:bg-red-500"
```

If you want a hover colour that only applies from a breakpoint up, say so:

```qml
Lo.style: "bg-white hover:bg-black md:hover:bg-blue-500"
```

`md:hover:` carries one state variant *and* a breakpoint, so it outranks plain
`hover:` when both match. The full ordering is in
[utilities.md](utilities.md#specificity--which-rule-wins).

## Changing the thresholds

A [design token file](configuration.md) can move or add thresholds:

```json
{ "schemaVersion": 2, "tokens": { "breakpoints": {
  "sm": 480, "md": 720, "lg": 960, "xl": 1200, "wide": 1680
} } }
```

Two rules the loader enforces:

- a threshold must be **greater than zero** — one at or below zero is met by
  every window, which makes the query meaningless rather than merely unusual, and
  is rejected;
- thresholds should be **strictly widening**. A threshold no wider than the one
  before it warns, because its classes can never be the narrowest matching
  min-width condition and will appear to do nothing.

Names are not structural; adding `wide` above creates the `wide:` and
`max-wide:` variants.

The built-in thresholds are also readable as typed `Loom.breakpoint` properties
for layout logic that needs the number rather than a variant.

## Testing responsive behaviour

Under `loom dev`, resize the window — conditions are recomputed on every width
change and styles re-apply immediately, with no reload.

In a test, drive a real `QQuickWindow`:

```cpp
QQuickWindow window;
window.resize(400, 300);           // below sm
window.show();
QVERIFY(QTest::qWaitForWindowExposed(&window));

// ... create the item, parent it into window.contentItem() ...

QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::white));
window.resize(800, 300);           // >= md
QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::black));
```

`QTRY_COMPARE` rather than `QCOMPARE` because applies are queued to the event
loop — see [performance.md](performance.md) for why.

## Cost

An item whose style uses any breakpoint variant connects to its window's
`widthChanged` signal. Every width change during a drag-resize therefore
schedules a re-apply for that item, even though the tier itself changes at most
four times across the whole drag. Applies are coalesced to one per event-loop
turn, so the cost is one apply pass per item per frame of the resize, not one
per pixel — but on a screen with hundreds of breakpoint-using items it is
measurable. [performance.md](performance.md) has the details and the mitigation.
