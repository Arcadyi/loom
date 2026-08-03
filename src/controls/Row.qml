pragma ComponentBehavior: Bound

import QtQuick
// Qualified, because this file *is* `Row` inside its own module: the implicit
// directory import would otherwise resolve the root element to this component
// and recurse.
import QtQuick as Quick

/*!
    A Row that can align its children on the cross axis.

    docs/styling/limitations.md listed container-level alignment as vocabulary
    Loom does not have, on the grounds that Qt Quick Layouts express alignment
    per child (`self-*` / `Layout.alignment`) rather than as a container
    property. That is true of Layouts and of QtQuick.Row, but not of
    QtQuick.Grid, which has had horizontalItemAlignment and
    verticalItemAlignment since 5.1. This gives Row the same thing.

    \qml
    Row {
        Lo.style: "gap-3 p-4"
        align: Qt.AlignVCenter

        Icon { }
        Text { text: qsTr("centred against the tallest sibling") }
    }
    \endqml

    Deliberately a QML property rather than an `items-center` utility class.
    A class would have to enter the catalogue, the LSP completion set, the
    documentation and tst_catalogue's round-trip -- all of which are hard to
    take back. The property is not, so the semantics get to survive contact
    with real use first.
*/
Quick.Row {
    id: root

    //! Qt.AlignTop (the QtQuick default), Qt.AlignVCenter, or Qt.AlignBottom.
    property int align: Qt.AlignTop

    // Safe to write y here: QQuickRow positions along x only and leaves y
    // untouched, so this is not fighting the positioner for the same property.
    function alignChildren(): void {
        const available = root.height - root.topPadding - root.bottomPadding;
        for (let i = 0; i < root.children.length; ++i) {
            const item = root.children[i];
            if (!item.visible)
                continue;
            let target = root.topPadding;
            if (root.align & Qt.AlignVCenter)
                target += (available - item.height) / 2;
            else if (root.align & Qt.AlignBottom)
                target += available - item.height;
            // Only on a real change. A y write still marks the child dirty, so
            // an unconditional one would re-enter through positioningComplete
            // and never settle.
            if (item.y !== target)
                item.y = target;
        }
    }

    // positioningComplete rather than childrenChanged: it fires after every
    // positioning pass, so it covers children being added, removed, shown,
    // hidden, and resized, which childrenChanged alone does not.
    onPositioningComplete: root.alignChildren()
    onAlignChanged: root.alignChildren()
    onHeightChanged: root.alignChildren()
    // Padding written *after* construction is ignored by QQuickBasePositioner:
    // it neither grows the implicit size nor re-offsets the children, so a bare
    // QtQuick.Row assigned topPadding from C++ keeps its children at y == 0.
    // Lo.style always writes late -- it applies through a queued invoke -- so
    // without this, `p-4` on a Row would set the property and change nothing on
    // screen. forceLayout() re-runs positioning against the current padding,
    // and its positioningComplete then realigns.
    onTopPaddingChanged: root.forceLayout()
    onBottomPaddingChanged: root.forceLayout()
    onLeftPaddingChanged: root.forceLayout()
    onRightPaddingChanged: root.forceLayout()
}
