pragma ComponentBehavior: Bound

import QtQuick
import Loom

/*!
    A selectable list row.

    The delegate idiom from docs/styling/cookbook.md, where selection had to be
    a ternary concatenated into the class string -- and written twice, once on
    the row and once on its label, because the two items cannot share one
    string:

        Lo.style: "rounded-md transition-colors duration-100 "
                 + (index === list.currentIndex ? "bg-accent" : "bg-surface ...")

    `selected` is a built-in Loom state, so declaring the property is all it
    takes for `selected:` to work on this item *and*, through `group-selected:`,
    on anything inside it.

    \qml
    ListView {
        delegate: ListRow {
            required property int index
            required property string name

            width: ListView.view.width
            selected: index === ListView.view.currentIndex
            text: name
            onClicked: ListView.view.currentIndex = index
        }
    }
    \endqml
*/
Box {
    id: row

    //! Drives the `selected:` variant here and `group-selected:` below.
    property bool selected: false
    property alias text: label.text
    signal clicked()

    // A group as well as a state, so descendants can react to the row's
    // selection without each being told about it.
    Lo.group: "row"
    Lo.style: "p-3 rounded-md transition-colors duration-100"
            + " bg-surface hover:bg-surface-alt selected:bg-accent"

    Text {
        id: label

        Lo.style: "text-sm text-foreground group-selected/row:text-on-accent"
        elide: Text.ElideRight
        width: row.availableWidth
    }

    TapHandler {
        onTapped: row.clicked()
    }
}
