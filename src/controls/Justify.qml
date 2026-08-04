pragma ComponentBehavior: Bound

import QtQuick

/*!
    Main-axis distribution values for \l Row and \l Col.

    \qml
    Row {
        Lo.style: "gap-3 w-full"
        justify: Justify.SpaceBetween
    }
    \endqml

    A type of its own rather than an enum on `Row`, because `Row` collides with
    QtQuick's. A colliding name still *instantiates* correctly -- the last
    import wins -- but as a value in an expression it resolves to the QtQuick
    type, which has no such enum, so `Row.SpaceBetween` evaluated to undefined
    at every call site that was not itself rooted in a Row. The symptom was
    the worst kind: the property took the undefined, fell back to 0, and a
    justify that read correctly in the source did nothing on screen.

    `Justify` collides with nothing, so it resolves the same way everywhere.
*/
QtObject {
    enum Mode {
        Start,
        Center,
        End,
        SpaceBetween,
        SpaceAround,
        SpaceEvenly
    }
}
