pragma ComponentBehavior: Bound

import QtQuick
import QtQuick as Quick
import "positioning.js" as Positioning

/*!
    A Column that can align its children on the cross axis.

    The vertical counterpart of Row. Named Col rather than Column because it
    does not collide: there is no reason to shadow QtQuick.Column when the
    shorter name is free, and `Col` reads the way the utility vocabulary does.

    \qml
    Col {
        Lo.style: "gap-2 p-6"
        align: Qt.AlignHCenter

        Text { text: qsTr("centred") }
        Text { text: qsTr("against the widest sibling") }
    }
    \endqml

    `justify` distributes along the column the way Row's does along the row,
    and shares its arithmetic through positioning.js -- see Row.qml for why
    only the arithmetic is shared and not the property plumbing.
*/
Quick.Column {
    id: root

    enum Justify {
        Start,
        Center,
        End,
        SpaceBetween,
        SpaceAround,
        SpaceEvenly
    }

    //! Qt.AlignLeft (the QtQuick default), Qt.AlignHCenter, or Qt.AlignRight.
    property int align: Qt.AlignLeft

    //! Where children sit along the column. Col.Start leaves positioning to
    //! the positioner, which is the QtQuick behaviour and the default.
    property int justify: Col.Start

    // See Row.qml: writing the axis the positioner owns re-enters through
    // positioningComplete, and this makes that re-entry a no-op.
    property bool _distributing: false

    function distributeChildren(): void {
        if (root.justify === Col.Start || root._distributing)
            return;
        const visible = [];
        const sizes = [];
        for (let i = 0; i < root.children.length; ++i) {
            const item = root.children[i];
            if (!item.visible)
                continue;
            visible.push(item);
            sizes.push(item.height);
        }
        const available = root.height - root.topPadding - root.bottomPadding;
        const offsets = Positioning.distribute(
            sizes, available, root.spacing, root.justify);
        if (!offsets)
            return;
        root._distributing = true;
        for (let j = 0; j < visible.length; ++j) {
            const target = root.topPadding + offsets[j];
            if (visible[j].y !== target)
                visible[j].y = target;
        }
        root._distributing = false;
    }

    // Safe to write x here: QQuickColumn positions along y only.
    function alignChildren(): void {
        const available = root.width - root.leftPadding - root.rightPadding;
        for (let i = 0; i < root.children.length; ++i) {
            const item = root.children[i];
            if (!item.visible)
                continue;
            let target = root.leftPadding;
            if (root.align & Qt.AlignHCenter)
                target += (available - item.width) / 2;
            else if (root.align & Qt.AlignRight)
                target += available - item.width;
            if (item.x !== target)
                item.x = target;
        }
    }

    onPositioningComplete: {
        root.alignChildren();
        root.distributeChildren();
    }
    onAlignChanged: root.alignChildren()
    onWidthChanged: root.alignChildren()
    onJustifyChanged: root.distributeChildren()
    onHeightChanged: root.distributeChildren()
    // See the note in Row.qml: QQuickBasePositioner ignores padding written
    // after construction, which is the only kind Lo.style ever writes.
    onLeftPaddingChanged: root.forceLayout()
    onRightPaddingChanged: root.forceLayout()
    onTopPaddingChanged: root.forceLayout()
    onBottomPaddingChanged: root.forceLayout()
}
