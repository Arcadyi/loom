pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import Loom

/*!
    A dialog with a styled panel and title bar.

    \qml
    Dialog {
        title: qsTr("Delete project?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        popupStyle: "rounded-xl"

        Label { text: qsTr("This cannot be undone.") }
    }
    \endqml

    \section2 Everything here is a part style, and it has to be

    A Popup is not an Item. `LoomStyleAttached` casts its target to a
    QQuickItem and warns when it cannot, so `Lo.style` on a Dialog does nothing
    at all -- it is not that the classes are ignored, it is that there is no
    item for them to be about. Nor can `Lo.group` be set here, so the parts
    cannot read the dialog's state the way a CheckBox's indicator reads its
    control's.

    That is why this type exposes more part styles than a Control-based one
    needs: `popupStyle` for the panel and `headerStyle` for the title bar are
    the only way in. It applies to every popup-based type -- \l Menu and
    \l Tooltip say the same thing.

    \section2 Context stops at the overlay

    A Popup renders in the window's overlay rather than inside whatever opened
    it, so container queries and `Lo.group` do not reach its contents. Classes
    on items *inside* the dialog work normally; classes that ask about an
    ancestor outside it do not.
*/
Controls.Dialog {
    id: root

    /*!
        \qmlproperty string Dialog::popupStyle
        \qmlproperty string Dialog::headerStyle

        Classes for the panel and for the title bar.
    */
    property string popupStyle
    property string headerStyle

    modal: true
    padding: Loom.space.s6

    anchors.centerIn: Controls.Overlay.overlay

    background: Rectangle {
        Lo.style: "bg-surface rounded-lg border border-outline shadow-lg "
                + root.popupStyle
    }

    header: Text {
        text: root.title
        visible: root.title.length > 0
        leftPadding: root.leftPadding
        rightPadding: root.rightPadding
        topPadding: root.topPadding
        bottomPadding: Loom.space.s2
        elide: Text.ElideRight
        Lo.style: "text-foreground text-lg font-semibold " + root.headerStyle
    }
}
