pragma ComponentBehavior: Bound

import QtQuick
import Loom

/*!
    An icon that takes its colour from a utility class.

    Loom has had icon recolouring since 0.4 -- \l{Loom::icon}{Loom.icon()}
    mints an `image://loom/...` URL that LoomIconProvider serves repainted,
    because Qt tints an icon item only while it is a mask and a `.svg`/`.png`
    source never is. What it did not have was a type. Two places in this
    repository wrote `Icon { }` in an example anyway -- Row.qml's docstring and
    docs/styling/components.md -- for something that did not exist. This is it.

    \qml
    Row {
        Lo.style: "gap-2"
        align: Qt.AlignVCenter

        Icon { name: "home" }
        Text { text: qsTr("Home") }
    }
    \endqml

    `text-*` reaches the colour the same way it reaches a Text, through the
    target profile: LoomTargetProfile::forType routes TextColor to `color` for
    an Image that declares one. So the state and responsive variants work here
    exactly as they do on a label.

    \qml
    Icon {
        name: "trash"
        Lo.style: "size-4 text-muted hover:text-danger transition-colors"
    }
    \endqml

    Sizing is `size-*`/`w-*`/`h-*` rather than a property. sourceSize follows
    the item's size, so an SVG rasterises at the size it is drawn at instead of
    being scaled after the fact.
*/
Image {
    id: root

    /*!
        The asset, resolved against \l{Loom::iconRoot}{Loom.iconRoot} when it is
        relative. `.svg` is appended when the final path segment carries no
        extension, so `"home"`, `"home.svg"` and `"outline/home"` all work; pass
        an absolute URL or one with a scheme to opt out of the root entirely.
    */
    property string name

    /*!
        The colour the asset is repainted in. Bound to the foreground token, so
        an icon nobody styles still follows the theme.

        A `text-*` class writes this property, and -- as everywhere else in Loom
        -- the first write replaces the binding for good. Bind it or style it,
        not both. See docs/styling/limitations.md.
    */
    property color color: Loom.color.foreground

    //! \internal Extension inferred from the last segment, so a dotted
    //! directory ("icons.v2/home") does not read as one.
    readonly property string _resolved: {
        if (root.name.length === 0)
            return "";
        const segment = root.name.slice(root.name.lastIndexOf("/") + 1);
        return segment.includes(".") ? root.name : root.name + ".svg";
    }

    source: root._resolved.length === 0 ? "" : Loom.icon(root._resolved, root.color)

    // width/height rather than the implicit pair, which QQuickImageBase makes
    // read-only: it derives them from sourceSize, and sourceSize is bound to
    // the item's size just below. Driving the explicit size instead leaves
    // implicitWidth reading a value nothing else feeds, so there is no cycle --
    // and `size-*`/`w-*`/`h-*` write exactly these two properties, so a class
    // still wins, the way it does everywhere else.
    width: Loom.space.s5
    height: Loom.space.s5

    // Rasterise at the drawn size rather than scaling afterwards. The style
    // applies one event-loop turn after creation (LoomStyleAttached::
    // scheduleApply), so a styled icon rasterises once at the default size and
    // again at the class's -- correct at both, and only ever one extra pass.
    sourceSize: Qt.size(root.width, root.height)
    fillMode: Image.PreserveAspectFit
    mipmap: true
}
