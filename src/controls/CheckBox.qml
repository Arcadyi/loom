pragma ComponentBehavior: Bound

import QtQuick
// Qualified for the reason Row.qml gives: this file *is* `CheckBox` inside its
// own module, so the implicit directory import would resolve the root element
// to this component and recurse.
import QtQuick.Controls as Controls
import Loom

/*!
    A checkbox whose indicator is reachable from a class string.

    QtQuick.Controls already implements everything a checkbox does; what it
    does not give you is anywhere for the utility vocabulary to write. The
    indicator is a style-provided delegate, and
    LoomStyleAttached::backgroundPath refuses anything that is not a Rectangle,
    so `bg-*` on a stock CheckBox styles nothing and warns. This replaces the
    delegates with Rectangles and hands them back through
    \l{CheckBox::indicatorStyle}{part styles}.

    \qml
    CheckBox {
        text: qsTr("Remember me")
        Lo.style: "gap-2 text-sm"
        indicatorStyle: "rounded-md"
    }
    \endqml

    The indicator reads the control's state through the group rather than
    directly: `checked` is duck-typed off the item that carries `Lo.style`, and
    the indicator is a Rectangle with no such property. `Lo.group` publishes it
    downward -- the same mechanism ListRow uses to let its label follow the
    row's selection, and the reason neither has to restate a ternary.

    Because the group name is set here, a call site that needs its own group
    should wrap this rather than reassign `Lo.group`.
*/
Controls.CheckBox {
    id: root

    /*!
        Classes for the box. Appended to the indicator's own, so an override
        keeps the rest -- see docs/styling/components.md.

        There is no `contentStyle`, and that is not an omission: the engine
        already routes `text-*` to a Control's contentItem and the whole font
        group to the Control itself, so `Lo.style: "text-sm text-muted"` styles
        the label from the call site. A part style here would be a second
        writer for the same property, which is a race rather than a feature.
    */
    property string indicatorStyle

    // Published so the two delegates below can read `checked`, `hovered` and
    // the rest off the control they belong to.
    Lo.group: "checkbox"

    spacing: Loom.space.s2

    // Transparent rather than absent, for the reason Box gives: `bg-*` needs a
    // Rectangle to write onto or it warns as unsupported.
    background: Rectangle {
        color: "transparent"
    }

    indicator: Rectangle {
        implicitWidth: Loom.space.s5
        implicitHeight: Loom.space.s5

        x: root.leftPadding
        y: root.topPadding + (root.availableHeight - height) / 2

        Lo.style: "rounded-sm border border-outline bg-surface"
                + " group-hover/checkbox:border-accent"
                + " group-checked/checkbox:bg-accent"
                + " group-checked/checkbox:border-accent"
                + " group-disabled/checkbox:opacity-50"
                + " transition-colors duration-100 " + root.indicatorStyle

        // The mark is a shape rather than a glyph on purpose: a checkmark
        // character depends on the font having one, and which fonts a design
        // file names is not knowable here.
        Rectangle {
            Lo.style: "center rounded-sm bg-on-accent"
            width: parent.width / 2.5
            height: parent.height / 2.5
            visible: root.checked
        }
    }

    contentItem: Text {
        text: root.text
        // Bound so `text-sm`/`font-medium` -- which land on the Control, not
        // here -- propagate down to the label.
        font: root.font
        // The indicator is not inside the content item, so the label clears it
        // itself. The control's own padding stays free for `p-*`.
        leftPadding: root.indicator.width + root.spacing
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        // A readable default that any `text-*` class overrides, the way
        // Button's label does it. Qt already dims this for the disabled state,
        // so no variant is needed -- and no Lo.style here, because the root's
        // `text-*` writes this same property through contentPath().
        color: root.palette.windowText
    }
}
