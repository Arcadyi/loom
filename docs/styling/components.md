# Components

```qml
import Loom
import Loom.Controls
```

Loom shipped no components for a long time, on the grounds that appearance was
its job and structure was QML's. That boundary held for placement, but not for
the handful of shapes every project rebuilds: a padded card, a row that centres
its children, a button, a field with an error line, a selectable list row. The
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

`Box`, `Col`, `Field` and `ListRow` do not collide with anything. `Col` is
spelled that way rather than `Column` for exactly that reason.

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

## Building without the module

`LOOM_BUILD_CONTROLS=OFF` leaves the styling library exactly as it was: Quick
only, no Quick Controls dependency, no `Loom.Controls` to import. Applications
built with `loom_add_application` link the module automatically when it exists,
so nothing in a project's own CMakeLists.txt refers to it either way. A
hand-rolled consumer links `loom::loomcontrols` and
`loom::loomcontrolsplugin` itself — see [../tooling/cmake.md](../tooling/cmake.md).
