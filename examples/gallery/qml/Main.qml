pragma ComponentBehavior: Bound
import QtQuick
import Loom
import Loom.Controls

Window {
    id: window

    // One list, not two index-aligned ones plus an integer. Navigation used to
    // be `pageNames` and `pageSources` kept in step by hand, a `currentPage`
    // int, and a showPage() function that had to stay imperative because a
    // seam Loader's source cannot be bound. Router and RouteView own all of
    // that now, and the route survives a hot reload as a bonus.
    readonly property var pages: [
        { route: "tokens", name: qsTr("Tokens"), source: "TokensPage.qml" },
        { route: "utilities", name: qsTr("Utilities"), source: "UtilitiesPage.qml" },
        { route: "states", name: qsTr("States"), source: "StatesPage.qml" },
        { route: "responsive", name: qsTr("Responsive"), source: "ResponsivePage.qml" },
        { route: "theming", name: qsTr("Theming"), source: "ThemingPage.qml" },
        { route: "features", name: qsTr("Modern"), source: "FeaturesPage.qml" },
        { route: "components", name: qsTr("Components"), source: "ComponentsPage.qml" }
    ]

    // Qt.resolvedUrl() here and not in RouteView: a relative string assigned
    // to a url property resolves against the file that does the assigning, and
    // that file is RouteView.qml inside the Loom.Controls module -- nowhere
    // near these pages. Resolving in the document that names them is the only
    // place the answer is right.
    readonly property var routes: {
        const map = {};
        for (const page of window.pages)
            map[page.route] = Qt.resolvedUrl(page.source);
        return map;
    }

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
                model: window.pages

                // Was a Rectangle + Text + MouseArea, with the selected style
                // written as a ternary over the whole class string and repeated
                // on both the chip and its label, because two items cannot
                // share one string. `selected:` is a variant now, and the label
                // reads the row's state through group-selected/row.
                ListRow {
                    required property var modelData

                    Lo.style: "mx-2"
                    width: parent.width
                    selected: modelData.route === Router.route
                    text: modelData.name
                    onClicked: Router.push(modelData.route)
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

    // Was a Flickable with four anchor lines, `clip`, and a hand-written
    // `contentHeight: pageLoader.height + 2 * Loom.space.s6` -- the same
    // arithmetic templates/app wrote differently. Padding is a class here
    // because Scroll declares the four property names the profile duck-types.
    Scroll {
        Lo.style: "p-6"
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        RouteView {
            routes: window.routes
            width: parent.width
            height: implicitHeight
        }
    }

    // The route outlives the scene, so this only fires on a genuinely fresh
    // start -- after a hot reload the previous page is still current.
    Component.onCompleted: {
        if (Router.route.length === 0)
            Router.push(window.pages[0].route);
    }
}
