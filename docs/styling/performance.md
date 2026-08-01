# Performance

What a styled item actually costs, what triggers work, and the three patterns
that make it expensive. Numbers here describe the current implementation rather
than a guaranteed contract.

The short version: **styling is cheap per item and cheap per apply; the cost
that bites is the number of *applies*, not the number of classes.**

## What a styled item allocates

| Thing | When | Cost |
| --- | --- | --- |
| A `LoomStyleAttached` object | any item with `Lo.style` | one QObject, plus a few signal connections |
| A compiled style | first time a unique *string* is seen anywhere in the process | shared by every item using that string |
| A watcher item | only when `hover:`, or `pressed:` on a target with no native `pressed` | one `QQuickItem` with a `HoverHandler` and a `TapHandler` |
| A shadow item | only while a `shadow-*` class is active | one `RectangularShadow` |

The compiled style is shared, not copied. A thousand items with
`Lo.style: "bg-surface rounded-lg"` parse that string once and hold a
`shared_ptr` to one compiled result. The vocabulary size is irrelevant to
runtime cost: only the classes you actually use are parsed.

The watcher and shadow are real items in the scene graph and are visible to code
walking `children`. Neither paints unless it has to — the watcher never does.

## The compile cache

Compiled styles live in a process-wide hash keyed by the exact string.

- A **cache hit is a hash lookup**. Binding `Lo.style` to an expression that
  flips between two values is therefore two compiles for the life of the
  process, not two per flip.
- **Whitespace and order matter to the key**, not to the result.
  `"bg-surface rounded-lg"` and `"rounded-lg bg-surface"` compile to the same
  behaviour but occupy two cache entries. Not a problem; worth knowing if you
  generate strings.
- The cache is **cleared when a design file loads**, because a config load can
  change which token names exist and a string compiled before a token existed
  had silently dropped the rule naming it.
- The cache is **not bounded**. A style string built from unbounded input —
  interpolating a row index, a timestamp, a user-entered value — grows it for
  the life of the process. Build such strings from a small fixed set of
  alternatives instead:

```qml
// Fine: two cache entries, forever.
Lo.style: selected ? "bg-accent rounded" : "bg-surface rounded"

// Not fine: one entry per distinct width, forever.
Lo.style: "bg-surface w-" + Math.round(fraction * 96)
```

## What triggers a re-apply

An item subscribes only to what its own style needs:

| Signal | Subscribed when |
| --- | --- |
| the token registry's `tokensChanged` | always — a theme switch or design reload |
| the registry's `vocabularyChanged` | always — forces a **recompile**, not just a re-apply |
| the window's `widthChanged` | the style uses any breakpoint variant |
| the parent's `widthChanged` / `heightChanged` | the style uses `w-full` / `h-full` |
| the item's `parentChanged` | the style uses `m-*` (margins re-route) or `w-full`/`h-full` |
| the item's `activeFocusChanged` | the style uses `focus:` |
| the item's `enabledChanged` | the style uses `disabled:` |
| the watcher's `hovered` / `pressed` | the style uses those variants |

An item styled `"bg-surface rounded-lg"` therefore wakes up only for a theme
switch. Variants are what buy subscriptions.

## The apply pass

Applies are **queued and coalesced**: a theme switch, a resize and a hover
arriving in one event-loop turn produce one apply, not three. The first apply is
also deferred by a turn, which is what lets `Lo.style` win over an item's own
initial property assignments regardless of declaration order.

One pass over a compiled style:

1. reads the current state bits and breakpoint tier;
2. walks the rules, discarding those whose tier or state does not match;
3. resolves each survivor's token name to a value against the active theme;
4. ranks by specificity into a small hash keyed by property path;
5. releases properties no longer wanted, restoring their saved values;
6. writes the rest — **skipping any write whose value already matches**.

Step 6 is why a theme switch between two themes that agree on a colour costs
nothing for the items using it, and why a resize that does not cross a
breakpoint writes nothing.

The pass constructs a `QQmlProperty` per managed property per apply, which is a
path parse plus a metaobject lookup. For a handful of properties per item this
is not measurable; across thousands of items applying every frame it is the
dominant cost. That is the first thing to fix if profiling ever points here.

## The three expensive patterns

### 1. Breakpoint variants during a drag-resize

Every item whose style uses `sm:`–`xl:` connects to its window's
`widthChanged`. During a drag-resize that fires continuously, so every such item
schedules an apply on every frame — even though the tier itself changes at most
four times across the whole drag, and every one of those applies writes nothing.

Coalescing keeps it to one pass per item per frame, not one per pixel. Still, on
a screen with hundreds of breakpoint-using items, this is the one pattern that
shows up in a profile.

Mitigation today: use breakpoint variants where they express something, not
reflexively. A design that reads the tier once, at a container, and lets the
children inherit through layout costs one subscription instead of hundreds.

### 2. `w-full` under an animated parent

`w-full` re-runs the whole apply pass on every parent size change — including
every frame of an animation on the parent — to write one number. If the parent
is animating, prefer an anchor or a `Layout.fillWidth`, which Qt updates
directly:

```qml
// Re-applies every frame while the parent animates.
Rectangle { Lo.style: "bg-surface w-full" }

// Costs nothing extra.
Rectangle { Lo.style: "bg-surface"; anchors.left: parent.left; anchors.right: parent.right }
```

### 3. Unbounded dynamic strings

See [the compile cache](#the-compile-cache). The symptom is memory growth
proportional to interactions rather than to the scene.

## Transitions

`transition-*` creates a `QPropertyAnimation` per animated property per write.
An interrupted animation is retargeted rather than restarted, so a rapidly
hovered item does not accumulate them.

The animation is created only when a covered property's value actually changes,
so `transition-all` on a static item costs nothing.

## Startup

Loading the token registry seeds every scale — 250-odd palette colours plus the
other nine scales — once per process. A design file load resets and re-seeds
them all, which is why the reset is a full rebuild rather than a diff: under
`loom dev` this happens on a debounce per save, and correctness there is worth
more than the microseconds.

Compiled styles are built lazily on first use, so application startup pays only
for the strings the first screen uses.

## Measuring

There is no benchmark suite in the repository yet. To measure a specific
concern:

- **Apply count** — enable the `loom.style` logging category and count warnings,
  or attach to `LoomTokenRegistry::tokensChanged` in a test.
- **Frame cost** — `QSG_RENDER_TIMING=1` separates scene-graph work from
  property writes. If styling is the problem it shows up as CPU time before the
  render, not in it.
- **Cache growth** — the compile cache has no introspection API; the proxy is
  process RSS over a long session with the interactions you suspect.

## Rules of thumb

- Styling a thousand static items is fine. Re-applying a thousand items every
  frame is not.
- Variants buy subscriptions. Use the ones you need.
- Keep dynamic style strings drawn from a small fixed set.
- Prefer anchors and Layouts for anything that tracks a continuously changing
  size; use `w-full` for the common static case.
- A property is either yours or Loom's — mixing them costs correctness before it
  costs performance. See
  [the conflict rules](utilities.md#conflict-rules--lostyle-versus-your-own-code).
