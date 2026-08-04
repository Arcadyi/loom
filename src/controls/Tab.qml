pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import Loom

/*!
    One button in a \l Tabs bar.

    \qml
    Tabs {
        Tab { text: qsTr("Overview") }
        Tab { text: qsTr("Activity"); indicatorStyle: "bg-success" }
    }
    \endqml

    `checked` is a built-in state, so the selected tab styles itself from the
    call site's own class string -- `checked:text-foreground` and the like --
    with no wiring. `indicatorStyle` is the underline that marks the selection,
    which is a child of the background and so out of routing's reach.
*/
Controls.TabButton {
    id: root

    //! Classes for the selection underline.
    property string indicatorStyle

    Lo.group: "tab"

    background: Rectangle {
        color: "transparent"

        Rectangle {
            height: 2
            width: parent.width
            y: parent.height - height
            visible: root.checked
            Lo.style: "bg-accent " + root.indicatorStyle
        }
    }

    contentItem: Text {
        text: root.text
        font: root.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        color: root.checked ? Loom.color.foreground : Loom.color.muted
    }
}
