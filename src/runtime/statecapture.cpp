#include "statecapture.h"

#include <QJsonValue>
#include <QMetaProperty>
#include <QObject>
#include <QQmlContext>
#include <QQmlProperty>
#include <QSet>
#include <QUrl>
#include <QtQml>

#include <private/qqmlproperty_p.h>

namespace loom {
namespace {

// Bundles are unpacked into a fresh directory on every reload, so the object's
// full base URL changes even when the file did not. The file name is the part
// that survives, which is what makes a key match across a reload at all.
QString keyFor(QObject *object)
{
    QQmlContext *context = qmlContext(object);
    if (!context)
        return {};
    const QString id = context->nameForObject(object);
    if (id.isEmpty())
        return {};
    return context->baseUrl().fileName() + QLatin1Char('#') + id;
}

bool isBound(QObject *object, const QString &property)
{
    const QQmlProperty qmlProperty(object, property, qmlContext(object));
    return qmlProperty.isValid() && QQmlPropertyPrivate::binding(qmlProperty) != nullptr;
}

// Declaring properties in QML makes the engine synthesise a subclass, and its
// name is the only public signal that the leaf metaobject is that synthesised
// type rather than a plain C++ one. The distinction is load-bearing:
// propertyOffset() excludes the base class's properties only when the leaf is
// synthesised. On a plain C++ leaf it points into that class's *own*
// properties, and the walk below would happily capture clip, opacity and
// rotation off an internal item inside Qt's own ApplicationWindow.qml.
bool hasQmlDeclaredType(const QObject *object)
{
    return QByteArray(object->metaObject()->className()).contains("_QML");
}

// Only the properties the QML file itself declared. Everything below
// propertyOffset() belongs to the C++ base type -- geometry, focus, colour --
// which the new scene sets up for itself and must not be overwritten with a
// snapshot of the old one.
QList<QMetaProperty> declaredProperties(const QObject *object)
{
    if (!hasQmlDeclaredType(object))
        return {};
    QList<QMetaProperty> properties;
    const QMetaObject *metaObject = object->metaObject();
    for (int index = metaObject->propertyOffset(); index < metaObject->propertyCount(); ++index) {
        const QMetaProperty property = metaObject->property(index);
        if (property.isWritable())
            properties.append(property);
    }
    return properties;
}

QList<QObject *> sceneObjects(QObject *root)
{
    QList<QObject *> objects{root};
    objects.append(root->findChildren<QObject *>());
    return objects;
}

// The directory the scene's own QML lives in. loom stages every qmlRoot into
// one bundle directory, so this is what separates the application's files from
// the QML that ships inside Qt.
QString sceneDirectory(QObject *root)
{
    QQmlContext *context = qmlContext(root);
    if (!context)
        return {};
    const QUrl base = context->baseUrl();
    if (base.isEmpty())
        return {};
    return base.adjusted(QUrl::RemoveFilename).toString();
}

// Qt's controls are QML too, and their internals carry ids and declared
// properties -- a TextField alone contributes selectionStartHandle and
// friends. Restoring a control's private state from a previous scene is not
// loom's business, and the file name it would be keyed under is not even
// the application's to keep unique.
bool belongsToScene(QObject *object, const QString &directory)
{
    QQmlContext *context = qmlContext(object);
    if (!context)
        return false;
    const QString base = context->baseUrl().adjusted(QUrl::RemoveFilename).toString();
    // A scene loaded from a relative URL has no directory to compare against.
    // Treating that as "everything belongs" would wave Qt's own controls
    // through, so it means the opposite: only equally rootless files match.
    if (directory.isEmpty())
        return base.isEmpty();
    return base.startsWith(directory);
}

} // namespace

QVariantMap captureSceneState(QObject *root)
{
    if (!root)
        return {};

    const QString directory = sceneDirectory(root);
    QVariantMap captured;
    // An id is only unique within its component, so a file instantiated more
    // than once -- a Repeater delegate, a reused page -- yields the same key
    // from several objects. Restoring one instance's state into another is
    // worse than restoring nothing, so an ambiguous key is dropped entirely.
    QSet<QString> ambiguous;

    for (QObject *object : sceneObjects(root)) {
        const QString key = keyFor(object);
        if (key.isEmpty() || !belongsToScene(object, directory))
            continue;
        if (captured.contains(key)) {
            ambiguous.insert(key);
            continue;
        }

        QVariantMap values;
        for (const QMetaProperty &property : declaredProperties(object)) {
            const QString name = QString::fromLatin1(property.name());
            if (isBound(object, name))
                continue;
            const QVariant value = property.read(object);
            // The scene this came from is about to be destroyed; anything that
            // is not plain JSON may be holding a pointer into it.
            if (QJsonValue::fromVariant(value).isUndefined())
                continue;
            values.insert(name, value);
        }
        if (!values.isEmpty())
            captured.insert(key, values);
    }

    for (const QString &key : ambiguous)
        captured.remove(key);
    return captured;
}

void applySceneState(QObject *root, const QVariantMap &state)
{
    if (!root || state.isEmpty())
        return;

    const QString directory = sceneDirectory(root);
    QSet<QString> seen;
    QSet<QString> ambiguous;
    for (QObject *object : sceneObjects(root)) {
        const QString key = keyFor(object);
        if (key.isEmpty() || !state.contains(key) || !belongsToScene(object, directory))
            continue;
        if (seen.contains(key)) {
            ambiguous.insert(key);
            continue;
        }
        seen.insert(key);

        if (!hasQmlDeclaredType(object))
            continue;
        const QVariantMap values = state.value(key).toMap();
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            // Re-checked against the new scene rather than trusted from the
            // capture: an edit can turn a stored property into a bound one,
            // and that binding is the newer intent.
            if (isBound(object, it.key()))
                continue;
            QQmlProperty property(object, it.key(), qmlContext(object));
            if (property.isValid() && property.isWritable())
                property.write(it.value());
        }
    }
}

} // namespace loom
