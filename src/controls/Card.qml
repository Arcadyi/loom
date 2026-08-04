pragma ComponentBehavior: Bound

import QtQuick
import Loom

/*!
    A padded surface with a border.

    \qml
    Card {
        Lo.style: "w-64"

        Col {
            Lo.style: "gap-2"

            Label { text: qsTr("Storage") }
            Label { Lo.style: "text-muted text-sm"; text: qsTr("41 GB of 100 GB") }
        }
    }
    \endqml

    The shape the cookbook carried as a recipe and every project then copied.
    A design file's `@card` covers the same ground and stays the better answer
    when a project wants its *own* card -- this is the one you get with no
    configuration at all, which is the difference that matters for a shipped
    component: it cannot require the application to have declared something
    before it renders correctly. `Field` ships on the same rule.

    Derives from \l Box, so padding is real and children land inside it. The
    defaults are property bindings rather than a class string, so `bg-*`,
    `rounded-*`, `border-*` and `p-*` at the call site replace them cleanly --
    see docs/styling/components.md on why a delegate never carries its own
    `Lo.style`.
*/
Box {
    padding: Loom.space.s6

    background: Rectangle {
        color: Loom.color.surface
        radius: Loom.radius.lg
        border.width: 1
        border.color: Loom.color.outline
    }
}
