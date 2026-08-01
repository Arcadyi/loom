pragma ComponentBehavior: Bound
import QtQuick
import Loom

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

        Column {
            Lo.style: "gap-1 mt-4"
            anchors.top: parent.top
            width: parent.width

            Repeater {
                model: window.pageNames

                Rectangle {
                    id: navEntry

                    required property int index
                    required property string modelData

                    Lo.style: index === window.currentPage
                        ? "bg-accent rounded-md mx-2 h-9"
                        : "bg-surface hover:bg-surface-alt rounded-md mx-2 h-9"
                    anchors.left: parent === null ? undefined : parent.left
                    anchors.right: parent === null ? undefined : parent.right

                    Text {
                        Lo.style: navEntry.index === window.currentPage
                            ? "text-on-accent text-sm font-medium ml-3"
                            : "text-foreground text-sm ml-3"
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: navEntry.modelData
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: window.currentPage = navEntry.index
                    }
                }
            }
        }

        Rectangle {
            Lo.style: "bg-surface-alt rounded-md mx-2 mb-4 h-9"
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right

            Text {
                Lo.style: "text-foreground text-sm"
                anchors.centerIn: parent
                text: Loom.dark ? qsTr("Switch to light") : qsTr("Switch to dark")
            }

            MouseArea {
                anchors.fill: parent
                onClicked: Loom.theme = Loom.dark ? "light" : "dark"
            }
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
            source: [
                "TokensPage.qml",
                "UtilitiesPage.qml",
                "StatesPage.qml",
                "ResponsivePage.qml",
                "ThemingPage.qml",
                "FeaturesPage.qml"
            ][window.currentPage]
        }
    }
}
