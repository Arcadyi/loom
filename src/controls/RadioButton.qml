pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import Loom

/*!
    A radio button with a reachable indicator.

    \qml
    Col {
        Lo.style: "gap-1"

        RadioButton { text: qsTr("Daily"); checked: true }
        RadioButton { text: qsTr("Weekly") }
    }
    \endqml

    Exclusivity is Qt's: radio buttons sharing a parent are already mutually
    exclusive, and a `ButtonGroup` covers the case where they do not. Nothing
    here reimplements that -- the delegates are replaced so `bg-*` and
    `rounded-*` have Rectangles to write onto, and that is all.
*/
Controls.RadioButton {
    id: root

    /*!
        Classes for the ring. There is no `contentStyle`: the engine already
        routes `text-*` to a Control's contentItem, so the label is styled from
        the call site's own `Lo.style`.
    */
    property string indicatorStyle

    Lo.group: "radio"

    spacing: Loom.space.s2

    background: Rectangle {
        color: "transparent"
    }

    indicator: Rectangle {
        implicitWidth: Loom.space.s5
        implicitHeight: Loom.space.s5

        x: root.leftPadding
        y: root.topPadding + (root.availableHeight - height) / 2

        Lo.style: "rounded-full border border-outline bg-surface"
                + " group-hover/radio:border-accent"
                + " group-checked/radio:border-accent"
                + " group-disabled/radio:opacity-50"
                + " transition-colors duration-100 " + root.indicatorStyle

        Rectangle {
            Lo.style: "center rounded-full bg-accent"
            width: parent.width / 2
            height: width
            visible: root.checked
        }
    }

    contentItem: Text {
        text: root.text
        font: root.font
        leftPadding: root.indicator.width + root.spacing
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        // No Lo.style: the root's `text-*` writes this property through
        // contentPath(), and two writers for one property is a race.
        color: root.palette.windowText
    }
}
