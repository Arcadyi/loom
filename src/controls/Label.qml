pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import Loom

/*!
    Text that wraps and follows the theme.

    QtQuick.Text defaults to no wrapping and to black, so every body paragraph
    in a loom project restates the same two lines. templates/app's home page
    sets `wrapMode: Text.WordWrap` four times; the gallery writes a muted
    caption -- `Text` plus `Lo.style: "text-muted text-sm"` -- seven times,
    under every heading, next to the SectionTitle helper that exists for the
    heading and not for the paragraph.

    \qml
    Col {
        Lo.style: "gap-2"

        Label {
            Lo.style: "text-muted text-sm"
            text: qsTr("Every token resolves through the active theme.")
            width: parent.width
        }
    }
    \endqml

    Derives from QtQuick.Controls.Label rather than from Text because the name
    collides with it, and this module's shadowing invariant is that a colliding
    type is always a strict superset of what it shadows -- tst_controls asserts
    it. QQuickLabel is itself a QQuickText, so `text-*` and `font-*` reach it
    the same way; deriving from the Control end also brings the four padding
    properties along, so `p-*` works here and would not on a bare Text.

    Wrapping still needs a width from somewhere -- a Layout, an anchor, or the
    parent -- because a Text cannot learn its container's width on its own.
    That part is QML's, and docs/styling/limitations.md says so under sibling
    anchors.

    `bg-*` does not apply: Label inherits Control's `background` slot but
    leaves it null, and LoomStyleAttached::backgroundPath refuses anything that
    is not a Rectangle. Put the Label in a Box when it needs a surface.

    `color` is a binding to the foreground token, so an unstyled Label follows
    a theme switch; a `text-*` class writes the property and, as everywhere in
    Loom, replaces that binding for good.
*/
Controls.Label {
    wrapMode: Text.WordWrap
    color: Loom.color.foreground
    font.pixelSize: Loom.text.base.size
}
