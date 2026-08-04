pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import Loom

/*!
    A small pill for a count or a status word.

    \qml
    Row {
        Lo.style: "gap-2"
        align: Qt.AlignVCenter

        Label { text: qsTr("Inbox") }
        Badge { text: "12" }
        Badge { text: qsTr("beta"); Lo.style: "bg-warning" }
    }
    \endqml

    A Control rather than a Rectangle, so `p-*` is real padding and the pill
    grows with its label instead of needing a width. That is the same reason
    \l Box exists.
*/
Controls.Control {
    id: root

    //! The label.
    property alias text: label.text

    leftPadding: Loom.space.s2
    rightPadding: Loom.space.s2
    topPadding: Loom.space.s0_5
    bottomPadding: Loom.space.s0_5

    background: Rectangle {
        color: Loom.color.accent
        radius: Loom.radius.full
    }

    contentItem: Text {
        id: label

        font: root.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: Loom.color.onAccent
    }
}
