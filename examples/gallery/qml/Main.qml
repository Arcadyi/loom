pragma ComponentBehavior: Bound
import QtQuick
import Loom
import Loom.Controls

Window {
    id: window

    property int currentPage: 0
    readonly property var pageNames: [
        qsTr("Tokens"),
        qsTr("Utilities"),
        qsTr("States"),
        qsTr("Responsive"),
        qsTr("Theming"),
        qsTr("Modern")
    ]
    readonly property var pageSources: [
        "TokensPage.qml",
        "UtilitiesPage.qml",
        "StatesPage.qml",
        "ResponsivePage.qml",
        "ThemingPage.qml",
        "FeaturesPage.qml"
    ]

    color: Loom.color.background
    width: 1100
    height: 720
    visible: true
    title: qsTr("Loom Gallery %1").arg(Loom.version)

    Rectangle {
        id: sidebar

        Lo.style: "bg-surface"
        width: 200
        height: parent.height

        Col {
            Lo.style: "gap-1 mt-4"
            anchors.top: parent.top
            width: parent.width

            Repeater {
                model: window.pageNames

                // Was a Rectangle + Text + MouseArea, with the selected style
                // written as a ternary over the whole class string and repeated
                // on both the chip and its label, because two items cannot
                // share one string. `selected:` is a variant now, and the label
                // reads the row's state through group-selected/row.
                ListRow {
                    required property int index
                    required property string modelData

                    Lo.style: "mx-2"
                    width: parent.width
                    selected: index === window.currentPage
                    text: modelData
                    onClicked: window.currentPage = index
                }
            }
        }

        ListRow {
            Lo.style: "mx-2 mb-4"
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            text: Loom.dark ? qsTr("Switch to light") : qsTr("Switch to dark")
            onClicked: Loom.theme = Loom.dark ? "light" : "dark"
        }
    }

    Flickable {
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        clip: true
        contentHeight: pageLoader.height + 2 * Loom.space.s6
        contentWidth: width

        Loader {
            id: pageLoader

            Lo.style: "mt-6 ml-6 mr-6"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
        }
    }

    // Assigned imperatively, never bound. This Loader is a hot-reload seam, and
    // ReloadController::reloadBoundaries() repoints a seam's `source` with
    // setProperty() to swap in the rebuilt file -- which destroys any binding on
    // it. As a binding, navigation worked until the first seam reload and then
    // silently stopped for the rest of the session.
    function showPage(index: int): void {
        pageLoader.source = window.pageSources[index] ?? "";
    }

    onCurrentPageChanged: window.showPage(window.currentPage)
    Component.onCompleted: window.showPage(window.currentPage)
}
