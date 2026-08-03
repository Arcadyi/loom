pragma ComponentBehavior: Bound

import QtQuick
import QtQuick as Quick

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
*/
Quick.Column {
    id: root

    //! Qt.AlignLeft (the QtQuick default), Qt.AlignHCenter, or Qt.AlignRight.
    property int align: Qt.AlignLeft

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

    onPositioningComplete: root.alignChildren()
    onAlignChanged: root.alignChildren()
    onWidthChanged: root.alignChildren()
    // See the note in Row.qml: QQuickBasePositioner ignores padding written
    // after construction, which is the only kind Lo.style ever writes.
    onLeftPaddingChanged: root.forceLayout()
    onRightPaddingChanged: root.forceLayout()
    onTopPaddingChanged: root.forceLayout()
    onBottomPaddingChanged: root.forceLayout()
}
