pragma ComponentBehavior: Bound

import QtQuick
// Qualified, because this file *is* `Row` inside its own module: the implicit
// directory import would otherwise resolve the root element to this component
// and recurse.
import QtQuick as Quick
import "positioning.js" as Positioning

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

    `justify` is the other axis: where the children sit *along* the row when
    they do not fill it. Qt Quick has no such property anywhere -- a positioner
    packs to the start and a Layout distributes through per-child
    `Layout.fillWidth` -- so `SpaceBetween` previously meant an invisible
    `Item { Layout.fillWidth: true }` spacer, which is what
    templates/app/qml/pages/HomePage.qml.in reaches for.

    \qml
    Row {
        Lo.style: "gap-3 w-full"
        justify: Row.SpaceBetween

        Text { text: qsTr("Title") }
        Text { text: qsTr("Edit") }
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

    enum Justify {
        Start,
        Center,
        End,
        SpaceBetween,
        SpaceAround,
        SpaceEvenly
    }

    //! Qt.AlignTop (the QtQuick default), Qt.AlignVCenter, or Qt.AlignBottom.
    property int align: Qt.AlignTop

    //! Where children sit along the row. Row.Start leaves positioning to the
    //! positioner, which is the QtQuick behaviour and the default.
    property int justify: Row.Start

    // Writing `x` is writing the axis QQuickRow owns, so a write re-enters
    // through positioningComplete. The guard makes that re-entry a no-op
    // rather than a loop; the idempotence check below then keeps a settled
    // layout from scheduling any further passes.
    property bool _distributing: false

    function distributeChildren(): void {
        if (root.justify === Row.Start || root._distributing)
            return;
        const visible = [];
        const sizes = [];
        for (let i = 0; i < root.children.length; ++i) {
            const item = root.children[i];
            if (!item.visible)
                continue;
            visible.push(item);
            sizes.push(item.width);
        }
        const available = root.width - root.leftPadding - root.rightPadding;
        const offsets = Positioning.distribute(
            sizes, available, root.spacing, root.justify);
        if (!offsets)
            return;
        root._distributing = true;
        for (let j = 0; j < visible.length; ++j) {
            const target = root.leftPadding + offsets[j];
            if (visible[j].x !== target)
                visible[j].x = target;
        }
        root._distributing = false;
    }

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
    onPositioningComplete: {
        root.alignChildren();
        root.distributeChildren();
    }
    onAlignChanged: root.alignChildren()
    onHeightChanged: root.alignChildren()
    onJustifyChanged: root.distributeChildren()
    onWidthChanged: root.distributeChildren()
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
