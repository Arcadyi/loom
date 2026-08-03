# Changelog

## Unreleased

**Components.** `Loom.Controls` is a new QML module: `Box`, `Row`, `Col`,
`Grid`, `Button`, `Field` and `ListRow`. Loom styled items and placed them but
shipped nothing to place, so the shapes every project needs lived in the
cookbook as recipes you copied and then owned. The keystone is `Box`: `p-*`
resolves to `topPadding` and a Rectangle has none, so a padded card meant an
inner item inset by `anchors.margins` plus `implicitHeight: child.implicitHeight
+ 2 * space` restated at every call site — three times in this repository
alone. `Box` derives from `Control`, which already has the padding properties
the target profile duck-types on, so it needed no engine change at all.

Types whose names collide with QtQuick ones derive from what they shadow, so
importing the module is always additive; `tst_controls` enforces it.
Applications built with `loom_add_application` get the module with no change to
their own CMakeLists.txt, which matters because every project `loom new` has
generated has its link line frozen in a file loom will never edit again.

**Application state variants.** A design file can declare states —
`"states": { "syncing": "..." }` — and use them as variant prefixes:
`syncing:border-warning`, `not-syncing:`, `group-syncing/row:`. Values come
from `Lo.states` or a bool property of the same name. Previously an
application-owned condition could only be a ternary concatenated into the class
string, repeated on every item that cared because two items cannot share one
string.

They are declared rather than invented at the call site because the compiler
caches by exact class string, process-wide. That also makes them free for
tooling: `parseVariant()` is shared by `compile()` and `unknownClasses()`, so
`loom style`, `loom lint` and `loom lsp` learn a project's states with no code
of their own — and a typo is still reported, which accepting arbitrary names
would have cost.

`invalid` joins the built-in states rather than being declared, because
`Field` ships using it and a component cannot require the application to have
configured something before it renders correctly.

**Fixes.**

- `loomSpecificity()` packs the state depth into six bits. The old worst case
  was around 53 and could not overflow; combining declared states with a group
  and a theme passes 63, where the shift would have wrapped rather than
  saturated and sorted the *most* specific rule below an unqualified one.
- `text-*` on a Control reaches its `contentItem`, mirroring the background
  delegation `bg-*` already had. `text-white` on a Button previously warned as
  unsupported and left the label the platform colour against whatever `bg-*`
  had just written.
- `Row` and `Col` force a layout when padding changes. `QQuickBasePositioner`
  ignores padding assigned after construction, and `Lo.style` only ever writes
  after construction — so `p-4` on a positioner set the property and rendered
  no differently.
- The gallery no longer binds a seam `Loader`'s `source`. `reloadBoundaries()`
  repoints a seam with `setProperty()`, which destroys the binding, so under
  `loom dev` gallery navigation stopped working after the first seam reload —
  in the example whose job is to demonstrate reloading.

## 0.4.0

- Introduced clean project/design schema v2 documents and `loom migrate --to 2`.
- Added recipes, arbitrary values with policy enforcement, dynamic viewport and
  container queries, system themes, complete token families, richer state/group
  variants, typography, transforms, cursors, gradients, rings, and opt-in filters.
- Added AST-backed lint/LSP diagnostics, JSON style reports, and an in-app
  Ctrl+Shift+I development inspector.
- Added Android, iOS, and embedded configure/build/deploy/dev adapters, hosted
  emulator validation, and native DEB/DMG/MSI release packaging.

## 0.3.0

**Editor IntelliSense.** `loom lsp` is a `qmlls` proxy that keeps Qt's normal
QML completion, linting, navigation, formatting and documentation, then adds
context-aware completion inside `Lo.style`. The same live compiler and token
registry also drive unknown-class diagnostics and fixes, resolved-value hovers,
and color previews. The nearest project's design tokens are watched and become
available without restarting the editor.

Pass ordinary language-server arguments after `--` (`loom lsp -- --build-dir
.loom/build/desktop-debug`) or select a Qt server explicitly with `--qmlls`.
CLion and IDEs that insist on an executable named `qmlls` can select the
installed compatibility shim under `<prefix>/<libexec>/loom/qmlls`; it forwards
the IDE's server arguments without replacing Qt's actual binary.

**Layout utilities.** loom could style an item but never place one, so every
layout decision fell back to raw QML — 37 `anchors.*` lines in the gallery
alone, inside an example whose job is to show you don't need them.

```qml
// Fills its parent, inset by 16px, in one string.
Rectangle { Lo.style: "bg-surface rounded-lg fill m-4" }
```

152 new classes. The vocabulary is 1702.

### Anchors and fill

`fill`, `fill-x`, `fill-y`, `center`, `center-x`, `center-y`, and the edge pins
`pin-t` / `pin-r` / `pin-b` / `pin-l`.

**These resolve to whichever layout system the item is actually in**, decided
per apply: anchors outside a `QtQuick.Layouts` layout, the `Layout.*` attached
properties inside one. `fill` is `anchors.fill` in an Item and
`Layout.fillWidth` + `Layout.fillHeight` in a ColumnLayout. Reparenting between
the two re-routes the write and releases the old one.

That is a correctness feature rather than a convenience: anchoring an item a
layout manages is undefined behaviour Qt warns about. It is the same routing
`m-*` has always done between `anchors.topMargin` and `Layout.topMargin`.

**Anchors are also what finally make `m-*` work.** Anchor margins only take
effect where an anchor line is set, which until now there was no way to do — so
`fill m-4` insets by 16 px, and `pin-l ml-6` sits 24 px from the left edge. The
two families were designed to compose.

### Layout-only

- `self-start` / `self-center` / `self-end` / `self-stretch` → `Layout.alignment`
- `min-w-{n}` `max-w-{n}` `min-h-{n}` `max-h-{n}` → the `Layout` size constraints
- `col-span-{n}` `row-span-{n}` → `Layout.columnSpan` / `rowSpan`

Qt Quick has no min/max or span concept off a layout, so outside one these warn
and skip, naming the reason rather than reporting "not supported on
QQuickRectangle" and sending you after the wrong problem.

### Aspect ratio

`aspect-square`, `aspect-video`, and any `aspect-{n}/{m}`. Width drives height,
re-derived whenever the width changes; inside a layout it sets
`Layout.preferredHeight` rather than writing `height` under a layout that owns
it.

### Also

- **The colour-opacity modifier no longer eats a slash.** `parseUtility` split
  on the last `/` before any utility matcher ran, so a slash could never be part
  of a class name. The whole name is tried first now, which is what lets
  `aspect-16/9` parse — and makes `w-1/2` report as an unknown class rather
  than being silently mangled into `w-1` with 2% alpha.
- Anchors are released with `QQmlProperty::reset()` when a class stops applying.
  Writing the saved value back cannot work for an anchor that was unset before:
  a default anchor line names no item and Qt refuses it, which would have left
  the item anchored forever.
- The gallery's `StatesPage.qml` lost all 10 of its `anchors.*` lines.

## 0.2.1

A correctness release, plus a documentation rewrite. Every fix below is a bug
that was live in 0.2.0, most of them in features the documentation already
promised worked.

**`ProtocolVersion` is now 2, so rebuild your application.** An application
built against 0.2.0 fails the handshake against a 0.2.1 `loom dev`, by name,
rather than reloading QML fine and then failing confusingly on the first design
save. See [docs/reference/upgrading.md](docs/reference/upgrading.md).

**Two behaviour changes worth checking**: specificity now ranks states above
breakpoints, and the managed shadow became a child of its target rather than a
sibling. Both are described under Styling below and in the upgrade guide.

### Styling

- **`hover:` no longer dies at desktop widths.** Breakpoint and state variants
  shared one "count of variant prefixes" specificity counter, so at equal counts
  the later-written class won: in `"hover:bg-accent md:bg-red-500"` the `md:`
  rule beat the `hover:` one at every width above 768, and hovering did nothing.
  They are now separate axes — a state variant outranks a breakpoint variant,
  and `md:hover:` outranks both.
- **Shadows no longer disturb layouts.** The managed `RectangularShadow` was a
  sibling parented into `target.parent`; inside a `Row`/`Column`/`Grid` or a
  Layout that made it a laid-out child of its own, taking a slot and fighting
  the positioner's writes. It is now a child of the target at `z: -1`, outside
  every layout. Shipped visibly broken in the gallery's own Theming and Tokens
  pages.
- **Rounded Controls cast rounded shadows.** `rounded-*` writes through to a
  Control's `background` delegate, but the shadow read `target.radius` —
  undefined on a Control, so 0. It now resolves the radius from the same place
  the box utilities write to.
- **`tracking-*` is order-independent.** Being em-relative, it resolved against
  whatever `font.pixelSize` held when its own rule was reached, so it was only
  correct when `text-{size}` appeared earlier in the string. It is now resolved
  after the pass that decides the size.
- `border-{n}` rejects `nan`, `inf` and negatives instead of writing them into
  the target's border.
- A theme colour can alias a semantic name it inherits (`"accent-hover":
  "accent"`); only the palette was consulted before, so such an alias silently
  produced an invalid colour. An unresolvable one now warns and is skipped.
- Breakpoint thresholds must be positive, tiers that are not strictly widening
  warn, and the tier walk stops at the first threshold the window does not meet.
- `hidden` is accepted as Tailwind spells it. It is a synonym for `invisible`
  today; Tailwind's `invisible` keeps the layout box, which Loom cannot yet
  express — see [docs/styling/limitations.md](docs/styling/limitations.md).
- Warnings name the utility family and token (`utility bg-* (blue-500) is not
  supported on QQuickText`). They printed the rule's key alone, which is empty
  for every flag utility, so an unsupported `italic` read `utility  is not
  supported on ...`.

### Hot reload

- **Relative `iconRoot` survives a design reload.** The runtime staged the
  received document into its own cache directory and reloaded from there, so a
  relative `iconRoot` resolved against the staging path — every icon in a
  project using one broke on the first design save under `loom dev`, while
  working in a compiled build. The `Design` frame now carries the document's
  path in the project, and the bytes never reach the filesystem. Reshaping that
  frame is what took **`ProtocolVersion` to 2**: a mismatched pair is now told
  so at the handshake instead of failing later with a parse error on one message
  type.
- **`loom.json` edits take effect.** The manifest was captured once at startup
  with no way to update it, so editing `qmlRoots`, `assetRoots`, `entry` or
  `design` rebuilt and restarted the application while the server went on
  bundling the roots the session began with. It is re-read after every rebuild.
- **A native rebuild refreshes the bundle.** The bundled `qmldir` comes from the
  build tree, so adding a `SINGLETONS` entry to CMake rebuilt, restarted, and
  served the *pre-rebuild* qmldir; the singleton was not one until an unrelated
  QML file was touched.
- A reload that no longer defines `iconRoot` clears it, matching the
  replace-don't-merge contract every other setting already followed.
- `qt.version` in `loom.json` is a minimum rather than an exact match, matching
  `find_package(Qt6 6.11)` and the documentation. Requiring equality would have
  invalidated every existing manifest the day 6.12 shipped.

### Build

- **The JSON-schema gate actually runs.** `loom_schema` skipped — reporting a
  pass — when `jsonschema` was missing, and CI never installed it while
  `CONTRIBUTING.md` claimed it did. CI now installs it and configures with the
  new `LOOM_STRICT_SCHEMA_TEST=ON`, which turns the skip into a failure.
- `LOOM_BUILD_E2E_TESTS=OFF` no longer compiles a full consumer project:
  `loom_e2e_consume` was registered outside the guard.
- **Two new tests close the gap the documentation sat in.** `loom_docs_style`
  runs the real class checker over every QML block in the documentation, so a
  class the docs promise cannot quietly stop existing; `loom_gallery_style`
  points it at the gallery's own 61 `Lo.style` literals, which nothing checked
  before.

### Documentation

Reorganised into `docs/styling/`, `docs/tooling/` and `docs/reference/`, with an
index at [docs/README.md](docs/README.md). Substantially rewritten rather than
moved:

- **[styling/utilities.md](docs/styling/utilities.md)** is now the complete
  reference: every family with the property it writes, and every scale with its
  values, so the numbers are no longer only in the source.
- **New:** [styling/cookbook.md](docs/styling/cookbook.md) (complete components
  and a migration path), [styling/performance.md](docs/styling/performance.md)
  (what a styled item costs and the three patterns that make it expensive),
  [reference/cpp-api.md](docs/reference/cpp-api.md) (the five public headers),
  and [reference/upgrading.md](docs/reference/upgrading.md).
- **[reference/architecture.md](docs/reference/architecture.md)** gained the
  styling pipeline it never described — the registry, compilation, target
  profiles, the apply pass, and why specificity has two axes.
- **[tooling/platforms.md](docs/tooling/platforms.md)** now leads with a status
  table. Android, iOS and embedded were written in a tense that read as though
  they worked; only `loom doctor` supports them.
- `MessageType::Design` is documented in the wire spec, with its payload and
  size cap — the headline 0.2.0 feature was missing from it entirely — and the
  `ReloadResult` example carries the `kind` field the server switches on.
- Three cross-references pointed at a `getting-started.md#deploying` section
  that does not exist, for an explanation that lived only in a CMake comment.
  The measurements now have a home in
  [tooling/platforms.md](docs/tooling/platforms.md#why-qt-is-not-bundled).
- `cmake-api.md` lists all four exported targets, not two; `utilities.md` no
  longer claims `Lo.style` re-asserts a property whose value has not changed;
  `cli.md` documents that `--verbose` is accepted and ignored.

## 0.2.0

**loom absorbed respin.** The two projects — utility-first styling, and the
Qt/QML build and hot-reload tooling — are now one framework, one package, one
binary. The name `respin` is gone: what was `respin new` is `loom new`,
`respin.json` is `loom.json`, `respin::Application` is `loom::Application`,
`respin_add_application` is `loom_add_application`, and `RESPIN_DEV_*` is
`LOOM_DEV_*`. A project runs `find_package(loom)` once and gets both halves.

### Live design tokens

The capability the merge existed for. `loom dev` already kept C++ services alive
across a QML reload; the token registry lives in that same surviving C++, so a
design file edit now **repaints the running window without recreating the scene
at all**. Nothing on screen loses its state — text stays typed, scroll positions
stay put.

- `loom.json` gains a `design` key naming a token file. `loom dev` watches it,
  `loom_add_application(... DESIGN ...)` compiles it into release builds, and
  `loom style` / `loom lint` load it so project-defined classes resolve.
- `loom::reloadConfig()` replaces rather than merges: tokens reset to the
  built-in set first, so a token deleted from the file stops resolving. A file
  that fails to parse changes nothing — which matters, because a file is
  malformed for most of the time someone is typing in it.
- Attached styles now **recompile**, not just re-apply, when a config changes
  which token names exist. A style string compiled before a token existed had
  already dropped the rule naming it, so re-applying alone re-applied the gap.
- On reload the active theme wins over the file's `defaultTheme`: someone who
  switched to dark to look at it and then saved meant to restyle dark.

### Tooling

- `loomstyle` is folded into the CLI as `loom style --check` / `--catalogue`.
- `loom lint` runs `qmllint` **and** the utility-class check — always both, so a
  `Lo.style` typo is never hidden behind an unrelated qmllint complaint.
- `loom doctor` reports on both halves, including the Loom QML module's
  `qmldir` and `qmltypes`.
- One scaffold template, and it is loom-styled. The `--loom` flag and the
  hand-synced `templates/app-loom/` overlay are gone.

### Packaging

- One `loomTargets` export and one `loomConfig.cmake` replace the two packages,
  exporting `loom::loom`, `loom::loomplugin`, `loom::Runtime` and
  `loom::Protocol`.
- `loom_add_application` defaults `IMPORT_PATHS` to `LOOM_QML_IMPORT_DIR`, so
  `import Loom` resolves for qmllint in a generated project with no wiring.
- New options `LOOM_BUILD_CLI` and `LOOM_BUILD_E2E_TESTS`. `LOOM_BUILD_CLI=OFF`
  still produces a usable styling-only package.
- `MessageType::Design` is additive, so `ProtocolVersion` stays at 1.

---

Everything below shipped while loom was a styling library only.

## Unreleased (pre-merge)

- **Colour opacity modifier**: `bg-surface/70`, `text-foreground/50`,
  `border-outline/25` — Tailwind's trailing `/0`–`/100` on the three colour
  families, composing with every variant. It scales the token's own alpha
  rather than replacing it, and stays attached to the token rather than a
  resolved value, so a theme switch re-resolves and re-applies it. Families
  with no alpha to modify (`w-full/70`) and out-of-range or non-numeric values
  are reported as unknown classes rather than silently dropping the modifier.

- **Recoloured icons**: `Loom.icon(source, color)` returns an image URL that
  serves an asset repainted in a token color, for `icon.source`, `Image.source`
  or anything else taking a URL. It exists because `icon.color` cannot do it —
  Qt tints a control's icon only while the icon item is a mask, which a plain
  file source never is, so the color is accepted and dropped. Recolouring
  happens in an image provider registered on first use, keeps the asset's
  coverage (soft edges stay soft), and overrides the black Qt resolves an
  SVG's `currentColor` to. Point `Loom.iconRoot` (or the config's `iconRoot`)
  at your icon directory once and call sites name assets bare —
  `Loom.icon("home.svg", Loom.color.foreground)`; a source with a scheme or a
  leading `/` bypasses the root. Pass `Loom.color.*` rather than a literal to
  keep the binding live across a theme switch.

- **`loomstyle` tool**: `--check` reports unknown `Lo.style` classes in QML
  literals at build time instead of at runtime (exit 1 on findings, so it drops
  into CTest or CI), and `--dump-catalogue` emits the whole utility vocabulary
  as JSON for editor completion. Both link the library, so they speak exactly
  what the application does; `--config` widens them with project-defined
  tokens. See [docs/tooling.md](docs/tooling/cli.md).
- **Catalogue API**: `<loom/loomcatalogue.h>` exposes `styleCatalogue()`,
  `styleCatalogueJson()` and `unknownStyleClasses()`. The token registry gained
  sorted key enumeration (`colorKeys()`, `spaceKeys()`, …) to back it. The
  catalogue is derived from the parser's own tables, and a test asserts every
  class it emits parses.
- **Box utilities reach Controls**: `bg-*`, `rounded*` and `border*` now route
  to a target's `background` delegate when the target is not itself a Rectangle
  but exposes one, so `Lo.style: "bg-surface rounded-full"` styles a Button
  without hand-writing a `background:` override. Duck-typed on the property
  name and resolved per instance; a background that is not a Rectangle still
  warns as unsupported. The write takes the property over, replacing the
  style's own down/hover colouring — restore it with `hover:`/`pressed:`.
- **Transition utilities**: `transition` / `transition-colors` /
  `transition-opacity` / `transition-all` / `transition-none`, `duration-*`
  and `ease-*` animate Loom's own property writes on theme switches, state
  flips and breakpoint changes; interrupted animations retarget smoothly.
- **Layout-aware margins**: `m-*` now writes the `Layout.*` attached margins
  when the item sits in a RowLayout/ColumnLayout/GridLayout (anchor margins
  otherwise), re-routing on reparent.

## 0.1.0 — 2026-07-30

First release. Utility-first styling for Qt QML with two layers over one
token registry:

- **Typed tokens** — the `Loom` singleton: full Tailwind color palette,
  semantic themable colors, spacing, typography, radius, shadows, opacity,
  durations, easing and breakpoint scales; autocompleted and qmllint-visible
  through generated qmltypes.
- **Utility strings** — the `Lo.style` attached property: `bg-* text-*
  font-* p-* m-* gap-* w-*/h-*/size-* rounded* border* opacity-*
  visible/invisible shadow-*` with `sm:/md:/lg:/xl:` responsive variants
  (window-width, mobile-first) and `hover:/pressed:/focus:/disabled:/dark:`
  state variants. Compiled once per unique string, applied with
  save/restore semantics and value diffing.
- **Theming** — built-in light/dark, runtime switching re-resolves both
  layers live.
- **Configuration** — JSON config for custom colors, spacing, breakpoint
  thresholds and themes (with inheritance); custom tokens usable from
  utility strings.
- **Packaging** — static QML module + plugin, `find_package(loom)` from
  build or install tree, zero runtime import setup; qmldir/qmltypes
  installed for consumer tooling.
- Gallery example app, unit + e2e test suites, CI with sanitizers and
  qmllint gate. Linux-validated; Qt 6.11, C++20.
