pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import Loom

/*!
    A switch with a reachable track and handle.

    \qml
    Switch {
        text: qsTr("Sync automatically")
        Lo.style: "text-sm"
        indicatorStyle: "group-checked/switch:bg-success"
    }
    \endqml

    Two parts rather than one: `indicatorStyle` is the track and
    `handleStyle` the knob that slides along it. Both read the control's
    `checked` through `Lo.group`, because a Rectangle has no such property of
    its own.

    The handle's travel is a QML `Behavior` and not a `transition-*` class.
    Loom's transitions animate Loom's own writes -- see
    docs/styling/utilities.md -- and `x` here is a binding the component owns,
    which is exactly the case the class does not cover.
*/
Controls.Switch {
    id: root

    /*!
        \qmlproperty string Switch::indicatorStyle
        \qmlproperty string Switch::handleStyle

        Classes for the track and the knob. There is no `contentStyle`: the
        engine already routes `text-*` to a Control's contentItem, so the label
        is styled from the call site's own `Lo.style`.
    */
    property string indicatorStyle
    property string handleStyle

    Lo.group: "switch"

    spacing: Loom.space.s2

    background: Rectangle {
        color: "transparent"
    }

    indicator: Rectangle {
        id: track

        implicitWidth: Loom.space.s11
        implicitHeight: Loom.space.s6

        x: root.leftPadding
        y: root.topPadding + (root.availableHeight - height) / 2

        // rounded-full rather than `radius: height / 2`, so the shape lives in
        // the same string a call site would override it from.
        Lo.style: "rounded-full bg-outline"
                + " group-checked/switch:bg-accent"
                + " group-disabled/switch:opacity-50"
                + " transition-colors duration-150 " + root.indicatorStyle

        readonly property real inset: 2

        Rectangle {
            width: track.height - 2 * track.inset
            height: width
            y: track.inset
            // visualPosition rather than position: it is the mirrored one, so
            // this travels the right way under an RTL layout.
            x: track.inset
                + root.visualPosition * (track.width - width - 2 * track.inset)

            Lo.style: "rounded-full bg-surface shadow-sm " + root.handleStyle

            Behavior on x {
                NumberAnimation {
                    duration: Loom.duration.d150
                    easing.type: Easing.OutCubic
                }
            }
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
