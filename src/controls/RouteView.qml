pragma ComponentBehavior: Bound

import QtQuick
import Loom

/*!
    The rendering half of \l Router.

    \qml
    RouteView {
        Lo.style: "fill"

        routes: ({
            "tokens": "TokensPage.qml",
            "utilities": "UtilitiesPage.qml"
        })
        fallback: "NotFound.qml"
    }
    \endqml

    driven by `Router.push("tokens")` from anywhere.

    `Router` has held the current route, its params and the history since 0.4,
    surviving a hot reload because it lives in the process-wide store rather
    than in a QML singleton the engine clears. What it had no answer for was
    what to *show*, so applications kept two index-aligned arrays -- one of
    names, one of filenames -- an integer, and a function to keep them in step.
    That is what examples/gallery/qml/Main.qml did.

    \section2 Why the source is assigned and not bound

    `ReloadController::reloadBoundaries()` repoints a seam Loader's `source`
    with setProperty(), and a property write destroys the binding on it
    permanently -- the rule docs/styling/limitations.md states for utility
    classes applies just as much here. A `source: routes[Router.route]` binding
    would therefore work perfectly until the first hot reload and then silently
    stop navigating, which is close to the worst failure shape available: the
    application still renders, and only stops responding to a Router call.

    So the source is assigned imperatively, and re-assigned when the Loader
    reports itself empty. That second half is what the hand-rolled version in
    the gallery lacked and would eventually have needed.
*/
Item {
    id: root

    /*!
        Route name to QML file. A plain map rather than a list of objects,
        because a route is looked up far more often than it is enumerated.
    */
    property var routes: ({})

    //! Shown when the current route names nothing. Empty renders nothing.
    property url fallback

    //! The Loader, for a call site that needs `item` or `status`.
    readonly property alias loader: loader

    function resolve(): url {
        const name = Router.route;
        if (name.length > 0 && root.routes && root.routes[name] !== undefined)
            return root.routes[name];
        return root.fallback;
    }

    function sync(): void {
        const target = root.resolve();
        if (loader.source !== target)
            loader.source = target;
    }

    Loader {
        id: loader

        anchors.fill: parent

        // A reload that blanked the seam leaves this at Null with a route
        // still set. Re-assigning here is what restores it; nothing else
        // will, because there is no binding left to re-evaluate.
        onStatusChanged: {
            if (loader.status === Loader.Null && Router.route.length > 0)
                root.sync();
        }
    }

    Connections {
        target: Router

        function onRouteChanged(): void {
            root.sync();
        }
    }

    onRoutesChanged: root.sync()
    onFallbackChanged: root.sync()
    Component.onCompleted: root.sync()
}
