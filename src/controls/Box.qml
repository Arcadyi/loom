pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

/*!
    A styled box with real padding.

    This is the component the utility engine could not be given as a class.
    `p-4` resolves to `topPadding`/`rightPadding`/`bottomPadding`/`leftPadding`
    (see LoomTargetProfile::forType), and a Rectangle has none of them -- so
    padding a card has always meant an inner item inset by `anchors.margins`
    and an `implicitHeight: content.implicitHeight + 2 * Loom.space.sN` written
    out by hand at every call site.

    Box derives from Control rather than Item because Control already
    implements, correctly, everything that construction was standing in for:
    the four padding properties the profile duck-types on, content-derived
    implicit sizing, and a `background` slot that LoomStyleAttached already
    routes `bg-*` / `rounded-*` / `border-*` onto. An Item with four declared
    padding properties satisfies the profile too, but sizing it means reaching
    for childrenRect on the Box itself, which feeds back the moment a child
    anchors to its parent.

    \qml
    Box {
        Lo.style: "@card p-6 bg-surface rounded-lg"

        Text { text: qsTr("padded by a class, not by arithmetic") }
    }
    \endqml

    One frame of deferral is expected: Lo.style applies through a queued invoke
    (LoomStyleAttached::scheduleApply), so padding -- and therefore
    implicitHeight -- settles one event-loop turn after creation. Inside a
    ColumnLayout that can show a single unpadded frame. It is the same deferral
    every other utility already has, not something specific to Box.
*/
Control {
    // Children land in the content item, so padding applies to them, and
    // `background` stays free for the style engine to write.
    default property alias content: holder.data

    // Transparent rather than absent: `bg-*` needs a background delegate to
    // write onto, and LoomStyleAttached::backgroundPath() refuses anything that
    // is not a Rectangle. Without this, `bg-surface` on a Box would warn as
    // unsupported instead of painting.
    background: Rectangle {
        color: "transparent"
    }

    contentItem: Item {
        id: holder

        // childrenRect is safe here and not on the Box itself: Control sizes
        // the content item from the outside, so a child measured against this
        // item is measured against a geometry the box did not derive from it.
        implicitWidth: childrenRect.width
        implicitHeight: childrenRect.height
    }
}
