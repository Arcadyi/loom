pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import Loom

/*!
    A slider with a reachable groove, fill and handle.

    \qml
    Slider {
        from: 0
        to: 100
        value: 40
        Lo.style: "w-64"
        trackStyle: "bg-success"
    }
    \endqml

    The channel is the control's background, so `bg-*` and `rounded-*` reach it
    from the call site with no part style at all. What they cannot reach is the
    filled portion drawn on top of it, and the knob -- hence `trackStyle` and
    `handleStyle`. Splitting those out is the difference between being able to
    colour a slider and having to replace its delegate.

    Horizontal only. `orientation: Qt.Vertical` still works -- it is Qt's
    property and nothing here removes it -- but the delegate geometry below
    lays out along x, so a vertical slider gets Qt's own groove back rather
    than a styled one. Stated rather than silently half-working.
*/
Controls.Slider {
    id: root

    /*!
        \qmlproperty string Slider::trackStyle
        \qmlproperty string Slider::handleStyle

        Classes for the filled portion and the knob.

        There is no `grooveStyle`, because Qt's Slider has no groove delegate:
        the channel *is* the background, so `bg-*` and `rounded-*` at the call
        site already reach it through the routing every Control gets. The fill
        drawn on top of it is a child that routing cannot see, which is what
        `trackStyle` is for.
    */
    property string trackStyle
    property string handleStyle

    Lo.group: "slider"

    implicitWidth: Loom.space.s48

    // The groove. Bindings rather than a class string, because the root's
    // `bg-*`/`rounded-*` are routed here by backgroundPath() and a second
    // writer for those properties would race with them.
    background: Rectangle {
        id: groove

        x: root.leftPadding
        y: root.topPadding + (root.availableHeight - height) / 2
        implicitWidth: Loom.space.s48
        implicitHeight: Loom.space.s1_5
        width: root.availableWidth
        height: implicitHeight
        radius: height / 2
        color: Loom.color.surfaceAlt

        Rectangle {
            width: root.visualPosition * groove.width
            height: parent.height
            Lo.style: "rounded-full bg-accent"
                    + " group-disabled/slider:opacity-50 " + root.trackStyle
        }
    }

    handle: Rectangle {
        x: root.leftPadding + root.visualPosition * (root.availableWidth - width)
        y: root.topPadding + (root.availableHeight - height) / 2
        implicitWidth: Loom.space.s5
        implicitHeight: Loom.space.s5

        Lo.style: "rounded-full bg-surface border-2 border-accent shadow-sm"
                + " group-pressed/slider:border-accent-hover"
                + " group-disabled/slider:opacity-50"
                + " transition-colors duration-100 " + root.handleStyle
    }
}
