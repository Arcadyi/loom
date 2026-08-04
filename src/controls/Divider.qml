pragma ComponentBehavior: Bound

import QtQuick
import Loom

/*!
    A one-pixel rule in the outline colour.

    \qml
    Col {
        Lo.style: "gap-3"

        Label { text: qsTr("Account") }
        Divider { width: parent.width }
        Label { text: qsTr("Billing") }
    }
    \endqml

    The cross-axis extent has to come from the call site -- `width` in a
    positioner, `Layout.fillWidth` in a Layout, an anchor otherwise. A
    Rectangle cannot learn its container's width by itself, and
    docs/styling/limitations.md documents that boundary rather than working
    around it.

    `color` is a binding rather than a default `Lo.style`, so a class string at
    the call site adds to this type instead of replacing what makes it visible:
    `Divider { Lo.style: "my-4" }` still has a colour.
*/
Rectangle {
    id: divider

    //! Qt.Horizontal (a rule across) or Qt.Vertical (a rule down).
    property int orientation: Qt.Horizontal

    readonly property bool _horizontal: divider.orientation === Qt.Horizontal

    color: Loom.color.outline

    // Thickness on the cross axis only. The main axis stays 0 so it is obvious
    // at the call site that the extent is still owed.
    implicitWidth: divider._horizontal ? 0 : 1
    implicitHeight: divider._horizontal ? 1 : 0
}
