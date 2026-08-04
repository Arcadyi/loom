pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import Loom

/*!
    A dropdown whose field, list and rows are all reachable from class strings.

    \qml
    Select {
        model: [qsTr("Daily"), qsTr("Weekly"), qsTr("Never")]
        Lo.style: "w-48"
        popupStyle: "rounded-lg"
    }
    \endqml

    Named `Select` rather than `ComboBox` because it does not shadow the
    QtQuick.Controls type -- it derives from it, so everything Qt documents
    still applies, but both names stay resolvable and mean the same thing. `Col`
    is spelled that way for the same reason.

    \section2 The popup does not inherit context

    A Popup renders in the window's overlay, not inside this item, so
    `Lo.group` and container queries do not cross that boundary: a
    `group-hover/select:` class on a row would never fire. The rows use their
    own `hover:` and `highlighted:` states instead, which reach them directly
    because an ItemDelegate carries both properties and the engine duck-types
    them. This is a property of Qt's overlay, not something Loom chose, and it
    applies to every popup-based control.

    \section2 The arrow is a glyph

    There is no bundled icon set, so the indicator is a text character rather
    than an asset -- which also means `indicatorStyle: "text-accent"` colours
    it the way it would colour any label. Replace `indicator` outright if you
    want a drawn shape or an \l Icon.
*/
Controls.ComboBox {
    id: root

    /*!
        \qmlproperty string Select::indicatorStyle
        \qmlproperty string Select::popupStyle
        \qmlproperty string Select::itemStyle

        Classes for the arrow, the dropdown panel and each row in it.

        The closed field is not on this list, and does not need to be: `bg-*`,
        `rounded-*` and `border-*` already reach its background and `text-*`
        its label, through the routing every Control gets. Part styles exist
        only for the delegates that routing cannot see.
    */
    property string indicatorStyle
    property string popupStyle
    property string itemStyle

    implicitWidth: Loom.space.s48

    // Bindings rather than a default `Lo.style`, and for a specific reason:
    // the root's `bg-*`/`rounded-*`/`border-*` are already routed *here* by
    // backgroundPath(). A class string on this delegate would be a second
    // writer for the same properties, and which one landed last would decide
    // the colour. The same rule Button follows for its label.
    background: Rectangle {
        color: Loom.color.surface
        radius: Loom.radius.md
        border.width: 1
        // Hover feedback survives as long as nobody writes `border-*` at the
        // call site; the first class write replaces this binding for good, the
        // way it does everywhere in Loom.
        border.color: root.hovered ? Loom.color.accent : Loom.color.outline
    }

    contentItem: Text {
        text: root.displayText
        font: root.font
        leftPadding: Loom.space.s3
        rightPadding: root.indicator.width + Loom.space.s2
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        color: root.palette.windowText
    }

    indicator: Text {
        x: root.width - width - Loom.space.s3
        y: root.topPadding + (root.availableHeight - height) / 2
        text: "▾"
        Lo.style: "text-muted text-sm " + root.indicatorStyle
    }

    delegate: Controls.ItemDelegate {
        id: option

        required property var modelData
        required property int index

        width: ListView.view.width
        highlighted: root.highlightedIndex === option.index

        background: Rectangle {
            // Its own states, not the Select's: see the note above about the
            // overlay boundary.
            Lo.style: "bg-surface hover:bg-surface-alt"
                    + " highlighted:bg-accent"
                    + " transition-colors duration-100 " + root.itemStyle
        }

        contentItem: Text {
            text: option.modelData
            leftPadding: Loom.space.s3
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            Lo.style: "text-foreground text-base"
                    + " group-highlighted/option:text-on-accent"
        }

        Lo.group: "option"
    }

    popup: Controls.Popup {
        y: root.height + Loom.space.s1
        width: root.width
        implicitHeight: Math.min(contentItem.implicitHeight, Loom.space.s64)
        padding: Loom.space.s1

        background: Rectangle {
            Lo.style: "bg-surface rounded-md border border-outline shadow-lg "
                    + root.popupStyle
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.delegateModel
            currentIndex: root.highlightedIndex

            Controls.ScrollIndicator.vertical: Controls.ScrollIndicator { }
        }
    }
}
