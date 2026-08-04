pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import Loom

/*!
    A tooltip with a styled panel.

    \qml
    Button {
        text: qsTr("Archive")

        Tooltip {
            text: qsTr("Move to the archive")
            visible: parent.hovered
        }
    }
    \endqml

    Named `Tooltip` rather than `ToolTip` so it does not shadow the
    QtQuick.Controls type, whose attached form -- `ToolTip.text` on any
    control -- stays available and unchanged. Shadowing a type that is used
    mainly through its attached property would have been a trap.

    Like every popup-based type, this is styled through part styles only: a
    Popup is not an Item, so `Lo.style` here has nothing to attach to and warns.
    See \l Dialog for the full explanation.
*/
Controls.ToolTip {
    id: root

    /*!
        \qmlproperty string Tooltip::popupStyle
        \qmlproperty string Tooltip::contentStyle

        Classes for the panel and for its text.
    */
    property string popupStyle
    property string contentStyle

    padding: Loom.space.s2
    delay: Loom.duration.d500

    background: Rectangle {
        Lo.style: "bg-foreground rounded-md " + root.popupStyle
    }

    contentItem: Text {
        text: root.text
        wrapMode: Text.WordWrap
        Lo.style: "text-background text-sm " + root.contentStyle
    }
}
