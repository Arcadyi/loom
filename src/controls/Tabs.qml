pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import Loom

/*!
    A tab bar with a styled rule underneath.

    \qml
    Col {
        Tabs {
            Lo.style: "w-full"

            Tab { text: qsTr("Overview") }
            Tab { text: qsTr("Activity") }
        }
    }
    \endqml

    Pairs with \l Tab, which is the button. Both are needed: a TabBar styles
    nothing about its buttons, so shipping only the bar would leave the part
    everyone actually looks at unstyled.

    `separatorStyle` is the rule along the bottom edge. The bar's own
    background is reachable from the call site the usual way.
*/
Controls.TabBar {
    id: root

    //! Classes for the rule under the bar.
    property string separatorStyle

    background: Rectangle {
        color: "transparent"

        Rectangle {
            height: 1
            width: parent.width
            y: parent.height - height
            Lo.style: "bg-outline " + root.separatorStyle
        }
    }
}
