#include "loomstatewatcher.h"

#include <QHash>
#include <QLoggingCategory>
#include <QQmlComponent>
#include <QQmlEngine>

Q_STATIC_LOGGING_CATEGORY(lcLoomState, "loom.style")

namespace {

// Two measures keep the watcher from disturbing the target's own input
// handling. The DragThreshold gesture policy keeps the TapHandler on a
// passive grab (cost: `pressed` reverts to false if the point drags past the
// threshold, which is the desired styling behavior anyway). And z: -1 stacks
// the watcher below every other child: a TapHandler stacked *above* a
// MouseArea sibling swallows its clicks even on a passive grab, while at the
// bottom of the stack both hover and press tracking still work on bare items
// and interactive children keep all their events.
constexpr const char watcherQml[] = R"(import QtQuick
Item {
    anchors.fill: parent
    z: -1
    property alias hovered: hoverHandler.hovered
    property alias pressed: tapHandler.pressed
    HoverHandler {
        id: hoverHandler
    }
    TapHandler {
        id: tapHandler
        gesturePolicy: TapHandler.DragThreshold
    }
}
)";

// The shadow follows the target through bindings, so reparenting, moving,
// resizing, per-corner radii and visibility all track without C++ plumbing.
// The undefined-checks make the same component work for non-Rectangle targets
// (no radius) and survive teardown ordering.
constexpr const char shadowQml[] = R"(import QtQuick
import QtQuick.Effects
RectangularShadow {
    required property Item target
    parent: target ? target.parent : null
    x: target ? target.x : 0
    y: target ? target.y : 0
    z: target ? target.z - 1 : 0
    width: target ? target.width : 0
    height: target ? target.height : 0
    radius: target && target.radius !== undefined ? target.radius : 0
    topLeftRadius: target && target.topLeftRadius !== undefined
        ? target.topLeftRadius : radius
    topRightRadius: target && target.topRightRadius !== undefined
        ? target.topRightRadius : radius
    bottomLeftRadius: target && target.bottomLeftRadius !== undefined
        ? target.bottomLeftRadius : radius
    bottomRightRadius: target && target.bottomRightRadius !== undefined
        ? target.bottomRightRadius : radius
    visible: target ? target.visible : false
    opacity: target ? target.opacity : 1
}
)";

QQmlComponent *cachedComponent(QQmlEngine *engine, const char *source)
{
    static QHash<QPair<QQmlEngine *, const char *>, QQmlComponent *> cache;
    const auto key = qMakePair(engine, source);
    if (QQmlComponent *cached = cache.value(key))
        return cached;
    auto *component = new QQmlComponent(engine, engine);
    // The URL must be resolvable by the engine or the component never leaves
    // the Loading state; an unregistered scheme (or a plain name) hangs it.
    component->setData(source, QUrl());
    cache.insert(key, component);
    QObject::connect(engine, &QObject::destroyed, [key] { cache.remove(key); });
    return component;
}

} // namespace

QQuickItem *loomCreateStateWatcher(QQuickItem *target, QObject *owner)
{
    QQmlEngine *engine = qmlEngine(target);
    if (!engine) {
        qCWarning(lcLoomState) << "Lo.style: hover:/pressed: need a QML engine; item"
                               << target << "was not created by one";
        return nullptr;
    }
    QQmlComponent *component = cachedComponent(engine, watcherQml);
    auto *watcher = qobject_cast<QQuickItem *>(component->create());
    if (!watcher) {
        qCWarning(lcLoomState) << "Lo.style: failed to create state watcher:"
                               << component->errorString();
        return nullptr;
    }
    watcher->setParent(owner);
    watcher->setParentItem(target);
    return watcher;
}

QQuickItem *loomCreateShadowItem(QQuickItem *target, QObject *owner)
{
    QQmlEngine *engine = qmlEngine(target);
    if (!engine) {
        qCWarning(lcLoomState) << "Lo.style: shadow-* needs a QML engine; item" << target
                               << "was not created by one";
        return nullptr;
    }
    QQmlComponent *component = cachedComponent(engine, shadowQml);
    auto *shadow = qobject_cast<QQuickItem *>(component->createWithInitialProperties(
        {{QStringLiteral("target"), QVariant::fromValue(target)}}));
    if (!shadow) {
        qCWarning(lcLoomState) << "Lo.style: failed to create shadow"
                               << "(is QtQuick.Effects available?):"
                               << component->errorString();
        return nullptr;
    }
    shadow->setParent(owner);
    return shadow;
}
