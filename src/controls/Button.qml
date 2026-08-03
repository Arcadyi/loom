pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls

/*!
    A Button that styles from a class string.

    Replaces the Rectangle + Text + MouseArea triple that the gallery writes
    out verbatim three times, and gives the cookbook's Button recipe a home.

    Deriving from Controls.Button is what makes it cheap: `pressed`, `down`,
    `checked` and `hovered` are already properties LoomStyleAttached reads for
    its state variants, `padding` is already what `p-*` duck-types onto, and
    `background` is already where LoomStyleAttached::backgroundPath() routes
    `bg-*` / `rounded-*` / `border-*`. Nothing here re-implements any of it.

    \qml
    Button {
        text: qsTr("Save")
        Lo.style: "px-4 py-2 rounded-lg bg-accent hover:bg-accent-hover"
                + " pressed:bg-accent-hover disabled:bg-surface-alt"
                + " transition-colors duration-150"
    }
    \endqml

    Spelling out `hover:` and `pressed:` is not boilerplate that survived by
    accident. The first `bg-*` write takes the property over from the built-in
    style, including its own interaction colouring, so a button that sets a
    background and no state variants would have no feedback at all. That is a
    property of the styling engine, not of this component -- see
    docs/styling/limitations.md on bindings versus writes.
*/
Controls.Button {
    id: control

    // A Text contentItem rather than the built-in label, so `text-*` reaches
    // it. Utilities cannot address a Control's internal label: the target
    // profile writes properties, and the label is not one.
    contentItem: Text {
        text: control.text
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        // A readable default that any `text-*` class overrides, rather than
        // leaving unstyled buttons black-on-accent.
        color: control.palette.buttonText
    }
}
