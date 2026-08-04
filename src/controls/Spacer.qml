pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

/*!
    Blank space that takes what is left over.

    `Item { Layout.fillWidth: true }` as a spacer is a QML idiom rather than a
    type, which is why it appears in templates/app/qml/pages/HomePage.qml.in
    with nothing naming what it is for.

    \qml
    RowLayout {
        Label { text: qsTr("Title") }
        Spacer { }
        Button { text: qsTr("Save") }
    }
    \endqml

    Inside a Layout it fills; inside a positioner it cannot, because Row and
    Column distribute nothing -- they place children end to end and stop. Give
    it an explicit `size` there.

    \qml
    Row {
        Icon { name: "home" }
        Spacer { size: Loom.space.s4 }
        Label { text: qsTr("Home") }
    }
    \endqml
*/
Item {
    id: spacer

    /*!
        Fixed extent on both axes. Negative means "fill instead", which is the
        default and the only thing that works in a Layout.
    */
    property real size: -1

    readonly property bool _fills: spacer.size < 0

    Layout.fillWidth: spacer._fills
    Layout.fillHeight: spacer._fills

    implicitWidth: spacer._fills ? 0 : spacer.size
    implicitHeight: spacer._fills ? 0 : spacer.size
}
