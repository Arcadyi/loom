pragma ComponentBehavior: Bound

import QtQuick
import QtQuick as Quick

/*!
    A Grid, present so the container vocabulary is complete.

    Unlike Row and Col this adds nothing: QtQuick.Grid has carried
    horizontalItemAlignment and verticalItemAlignment since 5.1, which is the
    cross-axis alignment Row and Col had to implement by hand. Shipping it
    anyway means `import Loom.Controls` gives you the whole set rather than two
    thirds of it, and it is where a future `columns` responsive default would
    go if one is ever wanted.

    \qml
    Grid {
        Lo.style: "gap-4"
        columns: width >= Loom.breakpoint.md ? 3 : 1
        verticalItemAlignment: Grid.AlignVCenter

        Card { }
        Card { }
        Card { }
    }
    \endqml

    Note that `columns` stays a QML binding. How many columns is a structural
    decision, and a utility class cannot make it -- see
    docs/styling/limitations.md.
*/
Quick.Grid {
}
