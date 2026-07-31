#pragma once

#include <QQuickItem>

// Creates the invisible child item that observes interaction state for
// `hover:` and `pressed:` variants: an anchors-filled wrapper holding a
// HoverHandler and a passive TapHandler, exposing `hovered` and `pressed`
// alias properties. Returns null (with a warning) when the target has no QML
// engine. The wrapper is QObject-parented to `owner` and item-parented to
// `target`.
QQuickItem *loomCreateStateWatcher(QQuickItem *target, QObject *owner);

// Creates the managed RectangularShadow sibling for shadow-* utilities: it
// lives next to the target (stacked just below it), and tracks the target's
// geometry, corner radius, visibility and opacity through bindings. Returns
// null with a warning when QtQuick.Effects is unavailable or the target has
// no QML engine. QObject-parented to `owner`.
QQuickItem *loomCreateShadowItem(QQuickItem *target, QObject *owner);
