pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import Loom

/*!
    A labelled text field with an error line.

    This is the component the `invalid:` variant was added for. The cookbook's
    Field recipe had to write

        Lo.style: "... " + (field.invalid ? " border-danger" : " border-outline")

    and said why: "the invalid state is a binding, not a variant, because
    'invalid' is your application's concept and loom has no variant for it".
    Declaring `property bool invalid` here is enough for the style engine to
    duck-type it -- the same way it already reads `checked` and `readOnly` --
    so the ternary collapses into the class string:

        Lo.style: "border-outline invalid:border-danger"

    and nothing at the call site has to restate it.

    \qml
    Field {
        label: qsTr("Email")
        invalid: !text.includes("@")
        placeholder: qsTr("you@example.com")
    }
    \endqml

    Requires `invalid` to be declared in the design file's "states" block. It
    is in the one `loom new` generates.
*/
Col {
    id: field

    property alias label: caption.text
    property alias text: input.text
    property alias placeholder: input.placeholderText
    //! Read by the `invalid:` variant, and shows the message below.
    property bool invalid: false
    property string message: qsTr("That does not look right.")

    Lo.style: "gap-1"

    Text {
        id: caption

        Lo.style: "text-muted text-sm font-medium"
        visible: text.length > 0
    }

    Controls.TextField {
        id: input

        width: field.width
        // The default border colour is a class, not a binding, so `invalid:`
        // can win it on specificity rather than by string surgery.
        Lo.style: "bg-surface rounded-md border border-outline px-3 py-2"
                + " text-base text-foreground focus:border-accent"
                + " invalid:border-danger"
        // No utility covers this one, so it comes from the typed API. Reaching
        // for Loom.color.* where the vocabulary does not reach is expected.
        placeholderTextColor: Loom.color.faint
    }

    Text {
        text: field.message
        visible: field.invalid
        Lo.style: "text-danger text-xs"
    }
}
