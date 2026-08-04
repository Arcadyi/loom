pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import Loom

/*!
    A menu with a styled panel and rows.

    \qml
    Menu {
        popupStyle: "rounded-lg"

        Action { text: qsTr("Rename") }
        Action { text: qsTr("Duplicate") }
        Action { text: qsTr("Delete") }
    }
    \endqml

    `Action` and `MenuItem` come from QtQuick.Controls and are unchanged; what
    this replaces is the panel and the row delegate, so `bg-*` and `rounded-*`
    have Rectangles to write onto.

    Styled through part styles only, for the reason \l Dialog gives at length:
    a Popup is not an Item, so `Lo.style` on a Menu has nothing to attach to.

    Rows read their own `hover:` and `highlighted:` states rather than the
    menu's, because `Lo.group` cannot be published from a Popup and would not
    cross the overlay boundary if it could.
*/
Controls.Menu {
    id: root

    /*!
        \qmlproperty string Menu::popupStyle
        \qmlproperty string Menu::itemStyle

        Classes for the panel and for each row.
    */
    property string popupStyle
    property string itemStyle

    padding: Loom.space.s1

    background: Rectangle {
        implicitWidth: Loom.space.s48
        Lo.style: "bg-surface rounded-md border border-outline shadow-lg "
                + root.popupStyle
    }

    delegate: Controls.MenuItem {
        id: entry

        // Bindings rather than a class string: these are the delegate's own
        // background and label, and this MenuItem is internal, so nothing else
        // writes them -- but keeping the form identical to the Control-based
        // types is what stops the two conventions drifting apart.
        implicitHeight: Loom.space.s9

        background: Rectangle {
            Lo.style: "bg-transparent rounded-sm hover:bg-surface-alt"
                    + " highlighted:bg-surface-alt"
                    + " transition-colors duration-100 " + root.itemStyle
        }

        contentItem: Text {
            text: entry.text
            font: entry.font
            leftPadding: Loom.space.s2
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            color: entry.enabled ? Loom.color.foreground : Loom.color.muted
        }
    }
}
