# Cookbook

Complete components, not fragments. Every utility string on this page is
checked by loom's own test suite, so if a class here does not exist the build
fails.

The recurring theme: **use semantic colours, let variants carry the states, and
keep layout in QML.** Loom styles items; it does not lay them out.

## Contents

- [A button](#a-button)
- [A card](#a-card)
- [A form field](#a-form-field)
- [A responsive page](#a-responsive-page)
- [Styling Quick Controls](#styling-quick-controls)
- [A list](#a-list)
- [Migrating an existing file](#migrating-an-existing-file)

---

## A button

The whole appearance, including every interaction state, in one string:

```qml
// Button.qml
import QtQuick
import QtQuick.Controls
import Loom

Button {
    id: control

    Lo.style: "bg-accent hover:bg-accent-hover pressed:bg-accent-hover"
             + " disabled:bg-surface-alt rounded-lg px-4 py-2"
             + " transition-colors duration-150"

    contentItem: Text {
        text: control.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        Lo.style: "text-on-accent text-sm font-medium disabled:text-faint"
    }
}
```

Three things are doing work here.

**`bg-*` on a Control routes to its `background` delegate.** A Control is not a
Rectangle, so `bg-accent`, `rounded-lg` and any `border-*` are written through
`background.color` and friends. You do not need to write a `background:`
override — but the write *takes the property over*, replacing the built-in
style's own hover and pressed colouring. That is why `hover:` and `pressed:` are
spelled out: without them the button would have no interaction feedback at all.

**`disabled:` is a variant, not a separate item.** `enabled` is inherited in Qt
Quick, so disabling a parent disables this button and its `disabled:` rules
apply.

**The label is styled separately.** `text-*` needs a type with a `color` and a
`font`; a Control has neither directly. Styling the `contentItem` reaches the
Text that actually paints.

### A secondary variant

Same shape, outlined instead of filled:

```qml
Button {
    Lo.style: "bg-surface hover:bg-surface-alt border border-outline"
             + " rounded-lg px-4 py-2 transition-colors duration-150"
}
```

### A destructive variant

```qml
Button {
    Lo.style: "bg-danger hover:bg-danger/90 rounded-lg px-4 py-2"
             + " transition-colors duration-150"
}
```

`bg-danger/90` scales the token's alpha to 90 %, which is a cheap hover state
when the palette has no `danger-hover`.

## A card

```qml
// Card.qml
import QtQuick
import Loom

Rectangle {
    id: card

    default property alias content: body.data

    implicitWidth: 320
    implicitHeight: body.implicitHeight + 32

    Lo.style: "bg-surface border border-outline rounded-xl shadow-md"
             + " dark:shadow-2xl transition-colors duration-200"

    Column {
        id: body
        Lo.style: "fill m-4 gap-3"
    }
}
```

Notes:

- **`p-4` would not work here.** A Rectangle has no `topPadding`, so padding
  utilities warn and skip. Real padding on a plain Item means an inner item
  inset from its parent — which is what `fill m-4` does above: `fill` anchors
  the column to the card, and `m-4` supplies the 16 px inset the anchors then
  honour. This is the single most common surprise coming from Tailwind; see
  [limitations.md](limitations.md).
- **`gap-3` on the Column** sets `spacing`, which is the positioner's own
  property and works exactly as expected.
- **`dark:shadow-2xl`** because the `shadow-md` alpha reads as almost nothing on
  a dark background. Shadows are not themeable per-theme yet, so the variant is
  the way to do it.
- The managed shadow is a child of the card at `z: -1`, so it does not disturb
  whatever lays the card out.

## A form field

```qml
// Field.qml
import QtQuick
import QtQuick.Controls
import Loom

Column {
    id: field

    property alias label: caption.text
    property alias text: input.text
    property bool invalid: false

    Lo.style: "gap-1"

    Text {
        id: caption
        Lo.style: "text-muted text-sm font-medium"
    }

    TextField {
        id: input
        width: parent.width

        Lo.style: "bg-surface rounded-md border px-3 py-2 text-base"
                 + " text-foreground focus:border-accent"
                 + (field.invalid ? " border-danger" : " border-outline")

        placeholderTextColor: Loom.color.faint
    }

    Text {
        text: qsTr("That does not look right.")
        visible: field.invalid
        Lo.style: "text-danger text-xs"
    }
}
```

- **`focus:border-accent`** uses `activeFocus`, which `TextField` sets when it is
  being typed into.
- **The invalid state is a binding**, not a variant, because "invalid" is your
  application's concept and loom has no variant for it. Concatenating into the
  string is the normal way to reach application state; the compile cache means
  each distinct result is compiled once.
- **`placeholderTextColor` has no utility**, so it comes from the typed API.
  Reaching for `Loom.color.*` for properties the vocabulary does not cover is
  expected, not a workaround.

## A responsive page

```qml
// Page.qml
import QtQuick
import QtQuick.Layouts
import Loom

Rectangle {
    Lo.style: "bg-background"

    ColumnLayout {
        Lo.style: "fill m-4 gap-4 md:gap-6"

        Text {
            text: qsTr("Dashboard")
            Lo.style: "text-foreground text-2xl md:text-3xl font-bold"
        }

        GridLayout {
            // How many columns is a structural decision: loom can place an item
            // in a grid but cannot decide the grid's shape.
            columns: width >= Loom.breakpoint.md ? 3 : 1
            Lo.style: "fill-x gap-4"

            Repeater {
                model: 6
                Rectangle {
                    implicitHeight: 120
                    Lo.style: "fill-x aspect-video bg-surface border border-outline"
                             + " rounded-lg shadow-sm"
                }
            }
        }
    }
}
```

Three different resolutions of `fill*` in one file, decided by each item's
parent:

- the **ColumnLayout** is a child of a plain Rectangle, so `fill` anchors it and
  `m-4` becomes the anchor margins;
- the **GridLayout** is inside the ColumnLayout, so `fill-x` becomes
  `Layout.fillWidth`;
- each **cell** is inside the GridLayout, so `fill-x` is `Layout.fillWidth`
  there too, and `aspect-video` sets `Layout.preferredHeight` rather than
  writing `height` under a layout that owns it.

You do not have to track which is which. The remaining division of labour:

- **Breakpoint variants handle appearance** — `md:gap-6`, `md:text-3xl`.
- **Structure is QML** — the column count, because "how many columns" is not
  something a class on a child can express.

Both react to the same window width, so they stay in step.

### Margins inside a Layout

`m-*` inside a `RowLayout`/`ColumnLayout`/`GridLayout` writes the `Layout.*`
attached margins — which requires the item's **own file** to import
`QtQuick.Layouts`:

```qml
import QtQuick
import QtQuick.Layouts    // required for m-* to resolve, even if this file
import Loom               // declares no Layout itself

Rectangle {
    Lo.style: "bg-surface mt-4"
}
```

Without that import the write cannot resolve and a warning names the item.
Attached types resolve through the document's imports, which is a QML rule loom
cannot work around.

## Styling Quick Controls

Controls are the main place the duck-typed model shows through.

| Control | What works | What to know |
| --- | --- | --- |
| `Button`, `ToolButton` | `bg-*` `rounded-*` `border-*` on the background; `p-*` directly | style `contentItem` for text |
| `TextField`, `TextArea` | everything, including `text-*` | it *is* text-like, so no delegate needed |
| `CheckBox`, `RadioButton` | `p-*` `gap-*` | the indicator is a separate delegate loom does not reach |
| `Slider`, `ProgressBar` | `p-*` | handle and track are delegates |
| `Pane`, `Frame`, `Popup` | `bg-*` `rounded-*` `border-*` `p-*` | |

The rule: loom reaches a Control's **background** and its **padding**, both by
duck typing. Anything painted by another delegate — an indicator, a handle, a
groove — needs that delegate styled, or replaced.

```qml
CheckBox {
    id: box
    Lo.style: "p-2 gap-2"

    indicator: Rectangle {
        implicitWidth: 20
        implicitHeight: 20
        x: box.leftPadding
        y: box.topPadding + (box.availableHeight - height) / 2
        Lo.style: box.checked ? "bg-accent rounded border border-accent"
                              : "bg-surface rounded border border-outline"
    }
}
```

For states a Control has but loom has no variant for — `checked`, `down`,
`highlighted` — bind the string, as above.

## A list

```qml
import QtQuick
import Loom

ListView {
    id: list
    Lo.style: "gap-1"
    clip: true

    delegate: Rectangle {
        required property string name
        required property int index

        width: list.width
        implicitHeight: 48

        Lo.style: "rounded-md transition-colors duration-100 "
                 + (index === list.currentIndex ? "bg-accent" : "bg-surface hover:bg-surface-alt")

        Text {
            text: parent.name
            // The icon/label row idiom: pinned left, centred vertically,
            // inset by the margin the anchor then honours.
            Lo.style: "center-y pin-l ml-3 text-sm "
                     + (index === list.currentIndex ? "text-on-accent" : "text-foreground")
        }
    }
}
```

`gap-1` on the ListView sets its `spacing`. `clip: true` will clip any managed
shadow on a delegate, so use a border rather than `shadow-*` inside a clipping
view.

Note the delegate's style is a binding over `currentIndex`. Each distinct result
compiles once and is then a cache hit, so scrolling a long list does not
re-parse anything.

## Migrating an existing file

Take a hand-styled component:

```qml
Rectangle {
    color: "#ffffff"
    radius: 8
    border.width: 1
    border.color: "#e2e8f0"

    Text {
        color: "#0f172a"
        font.pixelSize: 16
        font.weight: Font.Medium
    }
}
```

Convert in three passes.

**1. Replace literals with tokens, keeping the properties.** This is the safe
step — nothing about how the file works changes, and qmllint checks every name:

```qml
Rectangle {
    color: Loom.color.surface
    radius: Loom.radius.lg
    border.width: 1
    border.color: Loom.color.outline

    Text {
        color: Loom.color.foreground
        font.pixelSize: Loom.text.base.size
        font.weight: Loom.text.medium
    }
}
```

The component is now themed: switching to dark re-evaluates every one of those
bindings.

**2. Collapse to utility strings where the whole property set is Loom's.**

```qml
Rectangle {
    Lo.style: "bg-surface rounded-lg border border-outline"

    Text {
        Lo.style: "text-foreground text-base font-medium"
    }
}
```

**Do not do this to a property you also bind.** The first utility write destroys
the binding permanently — a property is either yours or Loom's. If `color` is
bound to application state, leave it bound and style the rest.

**3. Add the states you previously wrote out by hand.** A `MouseArea` plus a
`states`/`transitions` block usually collapses to:

```qml
Rectangle {
    Lo.style: "bg-surface hover:bg-surface-alt rounded-lg border border-outline"
             + " transition-colors duration-150"
}
```

Run `loom lint` after each pass. It catches both a mistyped class and any
qmllint regression from the edit.
