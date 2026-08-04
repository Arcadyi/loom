pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

/*!
    A scrollable viewport that measures its own content.

    The shape every page in this repository writes by hand, and writes
    differently each time: examples/gallery/qml/Main.qml derives contentHeight
    from a Loader's `height`, templates/app/qml/pages/HomePage.qml.in from a
    Column's `implicitHeight`, and both restate the same padding arithmetic in
    the expression. Six anchor lines and a sum, per scrollable surface.

    \qml
    Scroll {
        Lo.style: "p-6"

        Col {
            Lo.style: "gap-4"
            width: parent.width

            Text { text: qsTr("as tall as it needs to be") }
        }
    }
    \endqml

    Padding is a class here even though a Flickable has no padding properties:
    the target profile duck-types on the four conventional names
    (LoomTargetProfile::forType), so declaring them is enough for `p-6` to
    resolve. It is the same opt-in a user component gets by declaring
    `property real topPadding`, and the reason Box could be a Control rather
    than a special case in the engine.

    Scroll position survives a hot reload when the Scroll carries a QML `id`:
    loom::captureSceneState records writable properties of id'd objects, and
    contentY is one. Without an id it starts at the top, like anything else.
*/
Flickable {
    id: root

    // Content lands in a plain Item rather than directly in the Flickable, so
    // there is one thing to measure. Flickable's own contentItem holds the
    // scrollbar and whatever else attaches itself, which is not a size.
    default property alias content: holder.data

    // Declared, not inherited: this is what `p-*` writes onto. See above.
    property real topPadding: 0
    property real rightPadding: 0
    property real bottomPadding: 0
    property real leftPadding: 0

    contentWidth: width
    contentHeight: holder.implicitHeight + root.topPadding + root.bottomPadding
    clip: true
    // A viewport that rubber-bands past its own end reads as a bug on desktop,
    // which is where loom's scroll surfaces are. Flicking still decelerates.
    boundsBehavior: Flickable.StopAtBounds

    ScrollBar.vertical: ScrollBar { }

    Item {
        id: holder

        x: root.leftPadding
        y: root.topPadding
        width: root.width - root.leftPadding - root.rightPadding
        // Height is the derived dimension and width is not, so content sized
        // against `parent.width` is safe and content sized against
        // `parent.height` is a loop. Give it an implicit height instead.
        implicitHeight: childrenRect.height
        height: implicitHeight
    }
}
