#pragma once

#include <QVariantMap>

class QObject;

namespace loom {

// Automatic scene-state capture across a reload.
//
// Every QML-declared, writable property of every object carrying a QML `id` is
// recorded, so a scene keeps its state without the root funnelling anything
// through loomSaveState(). Properties are keyed "<file>#<id>" -- the QML id
// is recovered at runtime from the object's context, so nothing has to be
// annotated or given an objectName.
//
// This is the one place in loom that reaches into Qt private headers. It has
// to: QML-declared properties are not QProperty-backed, so QMetaProperty
// reports isBindable() == false for all of them and there is no public way to
// tell `property int doubled: base * 2` from a plain stored value. Writing a
// saved value over the former destroys the binding and freezes the property at
// a stale number, so a capture that cannot see bindings is not safe to have.
// Confining the dependency here keeps the blast radius to one file if a Qt
// patch release moves the API.
QVariantMap captureSceneState(QObject *root);

// Writes captured values back into a freshly built scene. Properties the new
// scene has already bound are skipped, for the same reason they were never
// captured: the binding is the more current description of the value.
void applySceneState(QObject *root, const QVariantMap &state);

} // namespace loom
