pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import Loom

/*!
    A progress bar with a reachable fill.

    \qml
    Progress {
        value: 0.4
        Lo.style: "w-64 bg-surface-alt"
        trackStyle: "bg-success"
    }
    \endqml

    The channel is the control's background, so `bg-*` and `rounded-*` reach it
    from the call site with no part style -- the same arrangement \l Slider
    uses, and for the same reason. `trackStyle` is the filled portion drawn on
    top of it, which no routing table mentions.

    `indeterminate: true` is Qt's and still works, but the animation it drives
    lives in the style's own contentItem, which this replaces. An
    indeterminate Progress therefore renders as an empty channel; use Qt's
    ProgressBar directly where that mode matters.
*/
Controls.ProgressBar {
    id: root

    //! Classes for the filled portion.
    property string trackStyle

    implicitWidth: Loom.space.s48

    background: Rectangle {
        implicitWidth: Loom.space.s48
        implicitHeight: Loom.space.s2
        color: Loom.color.surfaceAlt
        radius: height / 2
    }

    contentItem: Item {
        Rectangle {
            width: root.visualPosition * parent.width
            height: parent.height
            Lo.style: "rounded-full bg-accent " + root.trackStyle
        }
    }
}
