#pragma once

#include <QQmlPropertyMap>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// Process-wide storage behind the `Store` singleton. Separate from the QML
// facade for one reason: ReloadController::applyBundle() calls
// QQmlEngine::clearSingletons() on every full reload, so anything living in a
// QML singleton is destroyed with the scene. The registry outlives the engine,
// the facade is rebuilt from it, and shared state survives a reload without any
// participation from statecapture.
//
// This is the same split LoomTokens has over LoomTokenRegistry, and for the
// same reason -- it is why design tokens already survive every reload.
class LoomStoreRegistry : public QObject {
    Q_OBJECT

public:
    static LoomStoreRegistry *instance();

    QVariant value(const QString &key) const;
    bool contains(const QString &key) const;
    QStringList keys() const;
    QVariantMap values() const;

    //! False when the value cannot be represented as JSON; see the .cpp.
    bool setValue(const QString &key, const QVariant &value);
    void clear();

signals:
    void valueChanged(const QString &key, const QVariant &value);

private:
    explicit LoomStoreRegistry(QObject *parent = nullptr);

    QVariantMap m_values;
};

// `Store` in QML: a property map whose contents outlive the scene.
//
//     Store.route = "settings"
//     Text { text: Store.route ?? "home" }
//
// QQmlPropertyMap rather than a get()/set() pair because it gives per-key
// change notification that QML bindings actually track. A pair of invokables
// would need every reader to subscribe by hand.
class LoomStore : public QQmlPropertyMap {
    Q_OBJECT
    QML_NAMED_ELEMENT(Store)
    QML_SINGLETON

public:
    explicit LoomStore(QObject *parent = nullptr);

protected:
    QVariant updateValue(const QString &key, const QVariant &input) override;

private:
    bool m_seeding = false;
};
