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

Because the prefixes are min-width and cumulative, an item at 1400 px matches
`sm:`, `md:`, `lg:` **and** `xl:` — so write the base case unprefixed and layer
overrides upward, exactly as in Tailwind. There is no max-width variant.

## What drives the tier

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

For sizes that should track the *parent* rather than the window, use `w-full` /
`h-full`, which copy the parent's dimensions and keep copying them.

## How the tier is computed

Loom walks the four thresholds in order and stops at the first one the window
does not meet. The resulting tier is the number of thresholds passed: 0 for a
window narrower than `sm`, 4 for one at least as wide as `xl`.

Stopping at the first failure matters when a design token file has moved the
thresholds. If they are not strictly widening — say `md` is set below `sm` —
continuing the walk would let the narrower tier promote past a breakpoint whose
own threshold is unmet. Loom stops instead, and
[`loom lint`](../tooling/cli.md) warns at config load that the narrower tier
will shadow the wider one.

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

A [design token file](configuration.md) can move any of the four:

```json
{ "breakpoints": { "sm": 480, "md": 720, "lg": 960, "xl": 1200 } }
```

Two rules the loader enforces:

- a threshold must be **greater than zero** — one at or below zero is met by
  every window, which makes the tier meaningless rather than merely unusual, and
  is rejected;
- thresholds should be **strictly widening**. A tier no wider than the one
  before it warns, because its classes can never be the widest match and will
  appear to do nothing.

The four tiers are structural: they map to the four prefixes, so a config can
move them but cannot add a fifth or rename one.

The thresholds are also readable as `Loom.breakpoint.sm` … `Loom.breakpoint.xl`
for layout logic that needs the number rather than a variant.

## Testing responsive behaviour

Under `loom dev`, resize the window — the tier is recomputed on every width
change and re-applies immediately, with no reload.

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
