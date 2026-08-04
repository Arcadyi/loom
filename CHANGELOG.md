# Changelog

## Unreleased

**The examples use the framework.** `Loom.Controls` shipped `Box`, `Field`,
`Button` and `Grid`, and the gallery went on using none of them: 8 files, one
import of the module, two of its types. It kept the Rectangle + Text +
MouseArea triple that `Button`'s own docstring names as its reason to exist,
and reached for `spacing: Loom.space.sN` twenty times against `gap-*` -- a gap
a comment in `tst_controls` had already remarked on with nothing to stop it.

Migrated: navigation to `Router` + `RouteView` (two index-aligned arrays, an
integer and an imperative setter, replaced by one list), the hand-wired
`Flickable` to `Scroll` in both the gallery and the app template, twenty
`spacing:` assignments to `gap-*`, seven muted-caption `Text` items to `Label`,
five copies of one card shell to a `@state-card` recipe, and the template's
unnamed `Item { Layout.fillWidth: true }` to `Spacer`.

`loom_dogfood` keeps it that way, banning the constructions the components
replace and requiring the module to actually be imported.

**Not migrated, deliberately:** `StatesPage`'s Rectangle + Text + MouseArea
cards. They look like the triple `Button` replaces and are not -- the page
exists to demonstrate `hover:`, `pressed:`, `focus:` and `disabled:` on plain
items, and replacing them with a control would delete what it teaches. That
distinction is the sort of thing only migrating actually surfaces.

**`RouteView`.** The rendering half `Router` never had. `Router` has held the
route, its params and the history since 0.4, surviving a reload because it
lives in the process-wide store -- and it had no documentation, no example and
no user anywhere in the repository. Applications kept two index-aligned arrays
and an integer instead, which is what the gallery does.

The source is assigned rather than bound, and that is load-bearing.
`ReloadController::reloadBoundaries()` repoints a seam Loader's `source` with a
property write, and a property write destroys the binding on it permanently --
the rule limitations.md already states for utility classes. A
`source: routes[Router.route]` binding would navigate correctly until the first
hot reload and then silently stop, still rendering. `RouteView` assigns, and
re-assigns when the Loader reports itself empty; a binding-based version fails
`routeViewRestoresItsSourceAfterASeamReload` and nothing else.

No route guards, no nested routes, no transitions, no URL parsing, and
`Router.back()` still drops params. Documented rather than implied.

**Main-axis distribution.** `Row` and `Col` take a `justify` property --
`Start`, `Center`, `End`, `SpaceBetween`, `SpaceAround`, `SpaceEvenly`.

Qt Quick has no such property anywhere: a positioner packs to the start, and a
Layout distributes through per-child `Layout.fillWidth`. `SpaceBetween` meant
an invisible `Item { Layout.fillWidth: true }` spacer, which is what
templates/app reaches for.

A property and not a class, on the rule `align` already set: a class has to
enter the catalogue, LSP completion, the docs and `tst_catalogue`'s round-trip,
none of which is easy to take back. The `limitations.md` row that names
container alignment as missing vocabulary is updated rather than left stale --
it is still not a class, and now says why.

Unlike `align`, this writes the axis the positioner owns, so it re-enters
through `positioningComplete`; a guard makes the re-entry a no-op and
`colDistributionSettlesRatherThanLooping` asserts the layout settles. A loop
there would not crash, it would burn a core while rendering correctly. Children
that overflow are left to the positioner: spreading negative free space would
move them backwards past the container's edge.

The distribution arithmetic is shared through `src/controls/positioning.js`,
the property plumbing is not -- Row writes `x` and Col writes `y`, and passing
those in as strings would trade two readable loops for one unreadable one.
`loom_controls_qmldir` globbed `*.qml` only, so a script in QML_FILES escaped
the "you forgot to register it" guard; it now checks those too.

**Two lint rules that read a whole class string.** `duplicateClass` for the
same class twice, and `conflictingClass` for a class every one of whose writes
a later class in the same string repeats. Both are warnings; `loom lint` gains
`--json`, which `loom style` has had since it shipped.

What they do *not* report is the design. `conflictingClass` fires only when
**everything** a class writes is written again later, so `p-4 px-6` -- four
sides then two of them, the documented shorthand idiom -- is silent, and so is
`hover:bg-accent bg-surface`, where the conditions differ. The two branches of
a ternary are separate literals and are supposed to write the same property,
which is why the pass works per literal rather than per binding. Run over the
gallery, the docs and the templates, the rules produce no findings at all.

`// loom-ignore <code>` suppresses a code on the following line, and a bare
`// loom-ignore` suppresses all of them. Without that, a rule with any
false-positive rate is un-adoptable, and the honest response would have been to
make it so conservative it found nothing.

**Multi-line class strings.** A QML template literal now reads as a class
string, so a long list does not need a `+` at the end of every line:

    Lo.style: `p-4 bg-surface rounded-lg
               hover:bg-blue-600
               md:p-6`

One AST case in the scanner. An array form was considered and rejected:
`Lo.style` is a QString property, so a JS array coerces through toString() and
arrives comma-joined; making it work would mean changing the property's type,
which breaks the compile cache key, leaves every class in the list
undiagnosed, and turns the inspector's editable style field read-only on any
binding that used it. A substitution-free template costs none of that.

**Design-defined tokens are reactive.** `Loom.color["brand-500"]` and its camel
alias `Loom.color.brand500` re-evaluate on a theme switch and on a design
reload, like any built-in token. The same holds for every scale.

This removes a limitation the documentation stated twice. The typed `Loom.*`
surface is X-macro-generated from tables compiled into loom, so a name a design
file invents had no property -- only `Loom.color.value("brand-500")`, which
reads the registry once and leaves a binding stale. Both `templates/app` and
the gallery define a brand ramp, so every scaffolded project met this on its
first theme switch.

The groups are `QQmlPropertyMap`s now, which is what gives per-key change
notification that QML bindings actually track -- the same choice `LoomStore`
made, for the same reason, and not codegen: a generated property set cannot
work under `loom dev`, where a design change repaints without touching the
scene, and the X-macro tables are compiled into the shipped library rather than
into the application.

Seeding skips any name the tables already generated a property for. Inserting
over one would shadow the accessor that reads through the registry, which is
the thing that makes a theme switch work -- so the built-ins would have gone
stale in exactly the way the custom ones used to.
`customTokensDoNotShadowBuiltInNames` pins that.

`value()` still works and is still a snapshot. It is documented as the older
form now rather than as the only one.

**Overlays and display types.** `Dialog`, `Menu`, `Tooltip`, `Card`, `Badge`,
`Progress`, `Tabs` and `Tab`.

The overlays turned up a constraint nothing had documented: **a Popup is not an
Item**, so `Lo.style` on one does nothing at all -- `LoomStyleAttached` casts
its target to a QQuickItem and warns when it cannot. The classes are not
ignored; there is no item for them to be about. `Lo.group` cannot be published
from a Popup either, so their parts read their own states rather than the
popup's. That is why these types expose more part styles than a Control-based
one needs: `popupStyle`, `headerStyle`, `itemStyle` and `contentStyle` are the
only way in.

`Tooltip` is spelled that way so it does not shadow `QtQuick.Controls.ToolTip`,
whose attached form -- `ToolTip.text` on any control -- stays available.
Shadowing a type used mainly through its attached property would have been a
trap.

`Card`'s defaults are token bindings rather than the `@card` recipe, on the
rule `Field` already set: a shipped component cannot require the application to
have declared something before it renders correctly. A project that wants its
own card still writes `@card` in its design file.

**No `Toast`,** deliberately. A toast's value is the host and the queue that
decides what shows when, and that is application behaviour rather than styling
-- which the contract these types follow says they do not implement. Shipping
the styled panel without the queue would have been the half-feature the
tranching was meant to avoid.

**Form controls.** `CheckBox`, `Switch`, `RadioButton`, `Slider` and `Select`.
Each derives from its QtQuick.Controls type and replaces only the delegates the
style engine cannot reach; none reimplements behaviour. Exclusivity is still
Qt's, `ButtonGroup` still works, and every property Qt documents is still there.

They exist because a stock control has nowhere for the vocabulary to write. An
indicator is a style-provided delegate and
`LoomStyleAttached::backgroundPath()` refuses anything that is not a Rectangle,
so `bg-*` on a plain `CheckBox` styles nothing and warns.

Sub-delegates read the control's state through `Lo.group` rather than directly:
`checked` is duck-typed off the item carrying `Lo.style`, and a Rectangle has
no such property. That is the mechanism `ListRow` already used for its label.

**Part styles are only for parts the routing cannot reach,** which is narrower
than it sounds and worth stating because getting it wrong is invisible. The
engine already routes `bg-*`/`rounded-*`/`border-*` to a control's `background`
and `text-*` to its `contentItem`. So a `contentStyle` for a label -- or a
default `Lo.style` on a replaced `background` delegate -- is a *second writer*
for a property the root already writes, and whichever landed last would decide
the colour. `Select` was written that way first and the contract test caught
it.

The consequence: a replaced delegate takes plain property bindings as defaults,
never its own class string, exactly as `Button`'s label has always done
(`color: control.palette.buttonText`, "a readable default that any `text-*`
class overrides"). `CheckBox`, `Switch`, `RadioButton` and `Select` therefore
have no `contentStyle`, and `Slider` no `grooveStyle` -- Qt's Slider has no
groove delegate, the channel *is* the background.

**Structural components.** `Icon`, `Scroll`, `Label`, `Divider` and `Spacer`
join `Loom.Controls`. Each replaces a construction the repository was writing by
hand:

`Icon` is the sharpest of them, because its absence was already visible.
`Loom.icon()` has recoloured assets since 0.4 — Qt tints an icon item only
while it is a mask, and a file source never is — but there was no type, and two
examples in this repository instantiated `Icon { }` for one that did not exist.
A third, `Card { }`, was in `Grid`'s docstring. `loom_docs_types` now resolves
every type the documentation instantiates, which is the check whose absence let
all three survive; `loom_docs_style` only ever checked the classes inside them.

Colour reaches an `Icon` through `text-*`, with every state and responsive
variant that implies. That is the one engine change here: the target profile
routes `TextColor` to `color` for an `Image` that declares one. Deliberately
narrower than a bare `hasProperty("color")` test, which would have made
`text-red-500` repaint a Rectangle's fill.

`Scroll` is the hand-wired `Flickable` — six anchor lines and a `contentHeight`
sum — written once. This repository had two of them and they disagreed on how
to compute the height. It needed no engine change either: declaring the four
conventional padding property names is enough for `p-*`, because the profile
duck-types on the names rather than on the type.

`Label` shadows `QtQuick.Controls.Label` and so derives from it, not from
`Text`. Shadowing it with a `Text` subclass would have taken away the padding
and background `QQuickLabel` adds, breaking the module's own invariant. The
invariant test now covers `Button` and `Label`, not just `Row` and `Grid`.

**Part styles.** A control owns items a call site cannot reach — a `Field`'s
caption, input and error line are internal to `Field.qml`, and `Lo.style`
writes onto the item carrying it. The convention is a `<part>Style` string
property forwarded onto that part's own `Lo.style`, appended to the part's
classes rather than replacing them, so an override keeps what it did not
mention. `Field` gains `labelStyle`, `contentStyle` and `messageStyle`.

The engine needs none of this; the tooling does. A forwarded property is not an
attached one, so nothing about its shape says it carries classes — without
`src/cli/stylebindings.h`, every class string in the library's part-styling
surface would be uncompleted, undiagnosed, and unchecked by `loom lint`. Both
scanners read that list: the AST visitor and the heuristic one that answers
while a document will not parse. `loom_controls_partstyle` fails the build when
a control declares a part style the list does not know, because the failure it
prevents is silent.

The list is matched by name and without context, so an unrelated
`property string labelStyle` is diagnosed as if it carried classes. A `*Style`
suffix rule would have claimed far more names on far less evidence.

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

**The inspector edits source.** The Ctrl+Shift+I overlay's `Lo.style` line is
a text field: type a class string, press Return, and the development server
rewrites the literal in the project's file. Nothing is applied in the running
scene -- the edit goes to disk, the file watcher rebuilds, and the scene
updates through the ordinary reload path, so what is on screen always agrees
with what is in the file. Applying locally first would have hidden every
refusal below.

Refusals matter more than the feature. The server will not rewrite a binding
that is not a single string literal, because the inspector reports the
*evaluated* result and cannot say which branch of a ternary produced it;
will not write a class the compiler does not recognise, checked with the same
`unknownStyleClasses()` the linter uses; will not write when the literal no
longer says what the scene believes, meaning the file moved underneath; and
cannot name a file outside the project, because only paths the server itself
put in the bundle resolve. The write is atomic.

`ProtocolVersion` stays at 2. The frame is client-to-server only, so the
hazard is the other direction: an unknown message type is a *fatal* framing
error, so a newer runtime meeting an older server would lose hot reload for
the session by sending one frame. The server therefore advertises
`"capabilities": ["styleEdit"]` on every `Bundle`, which an older decoder
ignores and a newer runtime requires before offering the field. Bumping the
version instead would have stranded every already-built application, because
`loom::Runtime` is statically linked.

**`loom add`.** `loom new` was one-shot: after it, adding a page or a component
meant creating the file by hand and remembering the conventions -- the pragma,
the import order, where it belongs. `loom add page Settings` and
`loom add component Badge` write one file each, into the layout the scaffold
already uses.

Nothing is registered anywhere, and the command says so: loom_add_application
globs QML_ROOT with CONFIGURE_DEPENDS, so a new file is compiled in on the next
configure. The generated sources use only built-in utility classes, so they lint
clean in a project whose design file defines no recipes. An existing file is
never overwritten, and a name QML cannot resolve as a type -- anything not
starting upper-case -- is refused rather than written out as a file nothing can
refer to.

**Fixes.**

- The scaffolded design file declared a `syncing` state that nothing in the
  generated project used. The hero card now supplies it and outlines itself on
  unsaved changes, and the label beside the button reads the same state through
  `group-syncing/hero:` rather than restating the condition.
- `tst_states::variantComposition` failed on roughly half of runs: hover is
  synthesised from mouse *moves*, and an earlier test left the cursor on the
  point it moved to, so no hover was delivered. Pre-existing.
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
- The gallery no longer binds a seam `Loader`'s `source`, and the hazard around
  seams is now documented and pinned by tests. A seam reload leaves the
  document holding the Loader in the *previous* staging directory, so any URL
  that document re-resolves points at the pre-edit copy: edit the page you are
  looking at, navigate away and back, and the change silently reverts.
  Assigning rather than binding does not cure that — both resolve against the
  same stale base — but it stops the re-resolve happening spontaneously on any
  dependency change. Curing it properly needs a way to resolve a
  bundle-relative path against the active staging directory, which the runtime
  knows and QML cannot yet ask for.
- `loom::Application` replaces a native Quick Controls style with `Basic`
  before it loads a scene. macOS and Windows default to a style that refuses to
  have `background` and `contentItem` replaced, so on those two platforms every
  box utility on a Control and every type in `Loom.Controls` was skipped with a
  warning — a styled Button painted the system button and nothing else. A style
  the application names itself, through `QT_QUICK_CONTROLS_STYLE`, a
  `qtquickcontrols2.conf` or its own `QQuickStyle::setStyle()`, is left alone.
- An inspector style edit no longer rewrites the file's line endings. The
  rewrite opened the source in text mode, so on Windows a one-class edit
  returned every line of an LF-ended file as CRLF, and the user's diff said the
  whole file had changed.

**Shared state.** `Store` is a property map whose contents live in a
process-wide C++ registry rather than in the QML singleton, so they survive the
`clearSingletons()` a full reload performs — the answer to "where does state
two documents share live", which the seam rule otherwise leaves open. `Router`
is a thin facade over the same registry, so the current route and its history
survive a reload too, instead of the application snapping back to its first
page on every file save.

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
