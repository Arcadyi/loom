# Components

```qml
import Loom
import Loom.Controls
```

Loom shipped no components for a long time, on the grounds that appearance was
its job and structure was QML's. That boundary held for placement, but not for
the handful of shapes every project rebuilds: a padded card, a row that centres
its children, a button, a field with an error line, a selectable list row, a
scrollable page, an icon that takes the theme's colour. The
[cookbook](cookbook.md) carried them as recipes you copied and then owned.
`Loom.Controls` ships them instead.

Everything here is small. `Box` derives from `Control`, `Row` from
`QtQuick.Row`, `Button` from `QtQuick.Controls.Button`. None of them
reimplement styling: they exist so the utility vocabulary has something to
write onto.

## Shadowing

`Row` and `Grid` are QtQuick type names, and `Button` is a QtQuick.Controls
name. Importing `Loom.Controls` after them re-points those names at this
module, because QML resolves a type name to the **last** import that provides
it.

That is safe here because of one invariant, which `tst_controls` enforces:

> Every `Loom.Controls` type whose name collides with a QtQuick or
> QtQuick.Controls type **derives from that type**.

So the shadowing is always a strict superset — nothing is taken away, and code
written against `QtQuick.Row` keeps working when it resolves to
`Loom.Controls.Row`. If you would rather be explicit, import qualified:

```qml
import Loom.Controls as Lc

Lc.Row { }
```

`Row` and `Grid` shadow QtQuick names; `Button` and `Label` shadow
QtQuick.Controls ones. `Box`, `Col`, `Field`, `ListRow`, `Icon`, `Scroll`,
`Spacer` and `Divider` collide with nothing. `Col` is spelled that way rather
than `Column` for exactly that reason.

`Label` is worth a note, because it is the one that reads like it should be a
`Text` and is not. QtQuick.Controls has a `Label`, so shadowing it with a
`Text` subclass would have taken away the padding and background that
`QQuickLabel` adds — the invariant above, broken. Deriving from the Control end
costs nothing, and it is why `p-*` works on a `Label` and would not on a `Text`.

## The contract

Every type here follows the same four rules. They are not style preferences;
each one is a thing the engine cannot do if the rule is broken.

1. **Derive from the Qt Quick control. Never reimplement its behaviour.**
   These types exist so the vocabulary has something to write onto, not to
   replace Qt's.
2. **`background` must be a `Rectangle`,** even a transparent one.
   `LoomStyleAttached::backgroundPath` refuses anything else, so `bg-*` on a
   control with a null or non-Rectangle background warns as unsupported rather
   than painting. `Box` declares one for exactly this reason.
3. **`contentItem` must be reachable** for `text-*` to land on the label half.
   `Button` replaces its content item with a `Text` to get this.
4. **Every stylable sub-part gets a [part style](#part-styles).**

One consequence worth stating plainly: a control that replaces a delegate only
works under the `Basic` Quick Controls style. The native macOS and Windows
styles refuse delegate replacement outright, which is why
`loom::Application` sets `Basic` for you. See
[limitations](limitations.md#per-type-support).

## Part styles

A control owns items you cannot reach from the call site. A `Field`'s caption,
its input and its error line are all internal to `Field.qml`, and `Lo.style`
writes onto the item that carries it — so without something in between, the
parts are unstylable.

The convention is a `<part>Style` string property, forwarded onto that part's
own `Lo.style`:

```qml
Field {
    label: qsTr("Email")
    Lo.style: "gap-2"

    contentStyle: "text-lg py-3"
    messageStyle: "italic"
}
```

Classes are **appended** to the part's own, not substituted, so an override
keeps everything it did not mention. That works because later classes win at
equal specificity — the same rule that makes `p-4 px-6` mean what it looks
like.

### Only for parts the engine cannot reach

This is the rule that decides whether a part style should exist at all, and it
is narrower than it first looks. The engine already routes:

| From the call site | Lands on |
| --- | --- |
| `bg-*`, `rounded-*`, `border-*` | the control's `background` delegate |
| `text-*` (colour, alignment, elide, line height) | its `contentItem` |
| `p-*`, `gap-*`, and the whole font group | the control itself, and the font propagates down |

So a `contentStyle` for a control's label would be a **second writer for a
property the root already writes** — and which one landed last would decide the
colour. That is a race, not a feature. `CheckBox`, `Switch`, `RadioButton` and
`Select` therefore have no `contentStyle`: style their labels with
`Lo.style: "text-sm text-muted"` at the call site.

The same rule says what a delegate may contain. **A replaced delegate takes
plain property bindings as defaults, never its own `Lo.style`:**

```qml
// Wrong — races with any bg-* the call site writes.
background: Rectangle { Lo.style: "bg-surface rounded-md" }

// Right — a default the first class write cleanly replaces.
background: Rectangle {
    color: Loom.color.surface
    radius: Loom.radius.md
}
```

`Button`'s label has always done it this way (`color: control.palette.buttonText`,
described in its source as "a readable default that any `text-*` class
overrides"). Part styles are for `indicator`, `handle`, `popup` and the like —
delegates no routing table mentions.

Forwarding is ordinary QML and the engine knows nothing about it. The
*tooling* does: `src/cli/stylebindings.h` lists the property names, which is
what lets completion, hovers and `loom lint` see inside them. A part-style
property that is not on that list still works and is silently unchecked, so
`loom_controls_partstyle` fails the build rather than letting that happen
quietly.

The tradeoff is stated where the list lives: names are matched exactly and
without context, so an unrelated `property string labelStyle` holding
something that is not a class string will be diagnosed as if it were.

## Box

The one that removes the most code. `p-*` resolves to
`topPadding`/`rightPadding`/`bottomPadding`/`leftPadding`, and a `Rectangle`
has none of them — so a padded card used to mean an inner item inset by
`anchors.margins` plus height arithmetic restated at every call site:

```qml
// before
Rectangle {
    Lo.style: "@card"
    implicitHeight: body.implicitHeight + 2 * Loom.space.s6

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: Loom.space.s6
    }
}

// after
Box {
    Lo.style: "@card p-6"

    ColumnLayout { width: parent.availableWidth }
}
```

`Box` derives from `Control`, which already implements the four padding
properties the target profile duck-types on, content-derived implicit sizing,
and a `background` slot that `bg-*` / `rounded-*` / `border-*` are already
routed to. Children go into the content item, so padding actually insets them.

`availableWidth` and `availableHeight` are `Control`'s: the size inside the
padding. Use them when a child needs to fill the padded box.

## Row, Col and Grid

Positioners with cross-axis alignment.

```qml
Row {
    Lo.style: "gap-3 p-4"
    align: Qt.AlignVCenter

    Icon { }
    Text { text: qsTr("centred against the tallest sibling") }
}
```

| Type | `align` accepts | Derives from |
| --- | --- | --- |
| `Row` | `Qt.AlignTop`, `Qt.AlignVCenter`, `Qt.AlignBottom` | `QtQuick.Row` |
| `Col` | `Qt.AlignLeft`, `Qt.AlignHCenter`, `Qt.AlignRight` | `QtQuick.Column` |
| `Grid` | use `horizontalItemAlignment` / `verticalItemAlignment` | `QtQuick.Grid` |

`Grid` adds nothing — QtQuick.Grid has had item alignment since 5.1, which is
what `Row` and `Col` were missing. It ships so the set is complete.

Alignment is a QML property and not an `items-center` utility class. Adding a
class means adding it to the catalogue, LSP completion, the documentation and
`tst_catalogue`'s round-trip, none of which are easy to take back; a property
is. If the semantics survive real use, classes can follow.

**Padding on a positioner needs these types.** `QQuickBasePositioner` ignores
padding assigned after construction — it neither grows the implicit size nor
re-offsets children — and `Lo.style` only ever writes after construction,
through a queued invoke. `Row` and `Col` force a layout when padding changes;
a plain `QtQuick.Row` given `p-4` would set the property and render no
differently.

## Scroll

A viewport that measures its own content.

```qml
Scroll {
    Lo.style: "p-6"

    Col {
        Lo.style: "gap-4"
        width: parent.width
    }
}
```

Before this, every scrollable surface was a hand-wired `Flickable`: six anchor
lines and a `contentHeight` sum. This repository had two of them and they did
not agree — one derived the height from a `Loader`'s `height`, the other from a
`Column`'s `implicitHeight`, and both restated the padding arithmetic inside
the expression.

Padding is a class here even though a `Flickable` has no padding properties.
`Scroll` declares the four conventional names, and the target profile
duck-types on exactly those — the same opt-in your own components get from
`property real topPadding`, and the reason `Box` could be an ordinary `Control`
rather than a special case in the engine.

Two things it does not do:

- **`bg-*` does not apply.** A `Flickable` is neither a `Rectangle` nor a
  `Control` with a background slot. Put a `Box` inside, or behind.
- **Content sized against `parent.height` is a loop.** Height is the derived
  dimension — that is the whole point — so give content an implicit height and
  let the viewport follow it. Width is safe: it comes from the `Scroll`.

Scroll position survives a hot reload when the `Scroll` carries a QML `id`,
because [state capture](../reference/runtime-api.md) records writable
properties of id'd objects and `contentY` is one. Without an `id` it starts at
the top.

## Button

```qml
Button {
    text: qsTr("Save")
    Lo.style: "px-4 py-2 rounded-lg bg-accent hover:bg-accent-hover"
            + " pressed:bg-accent-hover disabled:bg-surface-alt"
            + " text-on-accent transition-colors duration-150"
}
```

`pressed`, `down`, `checked` and `hovered` are already properties the state
variants read, so the variants work with no wiring. `bg-*` lands on the
background delegate and `text-*` on the content item.

Spelling out `hover:` and `pressed:` is not optional decoration. The first
`bg-*` write takes the property over from the platform style, including its
own interaction colouring, so a button that sets a background and no state
variants has no feedback at all. See
[property writes versus bindings](limitations.md#property-writes-vs-bindings).

## Field

A labelled text field with an error line.

```qml
Field {
    label: qsTr("Email")
    placeholder: qsTr("you@example.com")
    invalid: !text.includes("@")
}
```

`invalid` is a [built-in state](states.md), so the error styling is a variant
rather than string surgery. The cookbook's version had to append
`(field.invalid ? " border-danger" : " border-outline")` to the class string,
and duplicate that onto every item that cared. Now the field's own class string
says `border-outline invalid:border-danger` and wins on specificity.

Your own components get the same treatment: declare `property bool invalid`
and the style engine duck-types it, the same way it reads `checked` and
`readOnly`.

The caption, the input and the error line are reachable through
[part styles](#part-styles) — `labelStyle`, `contentStyle` and `messageStyle`.

## CheckBox, Switch and RadioButton

```qml
Col {
    Lo.style: "gap-2"

    CheckBox {
        text: qsTr("Remember me")
        Lo.style: "text-sm"
        indicatorStyle: "rounded-md"
    }

    Switch {
        text: qsTr("Sync automatically")
        indicatorStyle: "group-checked/switch:bg-success"
    }
}
```

Each replaces its delegates with Rectangles and hands them back as part styles:
`indicatorStyle` on all three, plus `handleStyle` for the `Switch`'s knob. The
label needs no part style — see [the rule above](#only-for-parts-the-engine-cannot-reach).

The indicator reads the control's state through **the group**, not directly:
`checked` is duck-typed off the item carrying `Lo.style`, and a `Rectangle`
has no such property. `Lo.group` publishes it downward, which is the same
mechanism `ListRow` uses for its label. That is why the group name is set by
the component, and why a call site needing its own group should wrap rather
than reassign.

The `Switch`'s knob travels on a QML `Behavior`, not a `transition-*` class.
Loom's transitions animate Loom's own writes; `x` here is a binding the
component owns, which is exactly the case the class
[does not cover](utilities.md).

## Slider

```qml
Slider {
    from: 0
    to: 100
    value: 40
    Lo.style: "w-64 bg-surface-alt"
    trackStyle: "bg-success"
}
```

The channel is the control's **background**, so `bg-*` and `rounded-*` reach it
with no part style — Qt's `Slider` has no groove delegate. What routing cannot
see is the filled portion drawn on top of it and the knob, which is what
`trackStyle` and `handleStyle` are for.

Horizontal only. `orientation: Qt.Vertical` still works — it is Qt's property
and nothing here removes it — but the delegate geometry lays out along x, so a
vertical slider gets Qt's own groove back rather than a styled one.

## Select

```qml
Select {
    model: [qsTr("Daily"), qsTr("Weekly"), qsTr("Never")]
    Lo.style: "w-48"
    popupStyle: "rounded-lg"
    itemStyle: "rounded-md"
}
```

Named `Select` rather than `ComboBox` because it does not shadow the
QtQuick.Controls type — it derives from it, so everything Qt documents still
applies, and both names stay resolvable. `Col` is spelled that way for the same
reason.

**The popup does not inherit context.** A `Popup` renders in the window's
overlay, not inside the item, so `Lo.group` and container queries do not cross
that boundary: a `group-hover/select:` class on a row would never fire. The
rows use their own `hover:` and `highlighted:` states, which reach them
directly because an `ItemDelegate` carries both properties. This is a property
of Qt's overlay rather than something Loom chose, and it applies to every
popup-based control.

**The arrow is a glyph,** not an asset — there is no bundled icon set, so the
indicator is a text character, which also means `indicatorStyle: "text-accent"`
colours it like any label. Replace `indicator` outright for a drawn shape or an
`Icon`.

## ListRow

A selectable row, for `Repeater` and `ListView` delegates.

```qml
ListView {
    delegate: ListRow {
        required property int index
        required property string name

        width: ListView.view.width
        selected: index === ListView.view.currentIndex
        text: name
        onClicked: ListView.view.currentIndex = index
    }
}
```

`selected` is a built-in state, and `ListRow` is also a named group (`row`), so
anything inside it can react without being told about the selection:

```qml
Text { Lo.style: "group-selected/row:text-on-accent" }
```

That replaces the cookbook's ternary written twice — once on the row and once
on its label, because two items cannot share one class string.

## Icon

```qml
Row {
    Lo.style: "gap-2"
    align: Qt.AlignVCenter

    Icon { name: "home" }
    Label { text: qsTr("Home") }
}
```

Loom has recoloured icons since 0.4 — [`Loom.icon()`](tokens.md#icons--loomicon)
mints a URL served repainted, because Qt tints an icon item only while it is a
mask and a `.svg` or `.png` source never is. What was missing was a type, and
its absence was visible: two examples in this repository instantiated `Icon { }`
for something that did not exist.

`name` resolves against [`Loom.iconRoot`](tokens.md#icons--loomicon) when it is
relative, and `.svg` is appended when the last path segment has no extension —
so `"home"`, `"home.svg"` and `"outline/home"` all work. Pass an absolute URL
to opt out of the root.

Colour comes from `text-*`, with every variant that implies:

```qml
Icon {
    name: "trash"
    Lo.style: "size-4 text-muted hover:text-danger transition-colors"
}
```

That works because the target profile routes `TextColor` to `color` for an
`Image` that declares one — the same duck-typing that gives padding to anything
declaring `topPadding`. Size is `size-*`/`w-*`/`h-*` rather than a property,
and `sourceSize` follows it, so an SVG rasterises at the size it is drawn at
instead of being scaled afterwards.

There is no bundled icon set. `Loom.iconRoot` points at assets you own, and
Qt's SVG renderer still resolves `currentColor` to black — the provider's
recolour is the workaround, and it only makes sense for single-colour assets.

## Label

Text that wraps and follows the theme. `Text` defaults to no wrapping and to
black, so a body paragraph restates the same two lines every time.

```qml
Col {
    Lo.style: "gap-2"

    Label {
        Lo.style: "text-muted text-sm"
        text: qsTr("Every token resolves through the active theme.")
        width: parent.width
    }
}
```

Wrapping still needs a width from a Layout, an anchor, or the parent — a `Text`
cannot learn its container's width on its own, and that boundary is
[QML's](limitations.md). `bg-*` does not apply, for the reason in
[the contract](#the-contract): `Label` leaves `Control`'s background slot null.
Put it in a `Box` when it needs a surface.

## Divider and Spacer

```qml
Col {
    Lo.style: "gap-3"

    Label { text: qsTr("Account") }
    Divider { width: parent.width }
    Label { text: qsTr("Billing") }
}
```

`Divider` is a one-pixel rule in the outline colour. The cross-axis extent has
to come from the call site — a `Rectangle` cannot learn its container's width
by itself. Its colour is a binding rather than a default class string, so
`Divider { Lo.style: "my-4" }` still has one; a default `Lo.style` would have
been replaced wholesale by that override.

`Spacer` is blank space that takes what is left over. Inside a Layout it fills;
inside a positioner it cannot, because `Row` and `Column` distribute nothing —
they place children end to end and stop. Give it an explicit `size` there.

```qml
RowLayout {
    Label { text: qsTr("Title") }
    Spacer { }
    Button { text: qsTr("Save") }
}
```

## Building without the module

`LOOM_BUILD_CONTROLS=OFF` leaves the styling library exactly as it was: Quick
only, no Quick Controls dependency, no `Loom.Controls` to import. Applications
built with `loom_add_application` link the module automatically when it exists,
so nothing in a project's own CMakeLists.txt refers to it either way. A
hand-rolled consumer links `loom::loomcontrols` and
`loom::loomcontrolsplugin` itself — see [../tooling/cmake.md](../tooling/cmake.md).
