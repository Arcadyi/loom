#include "loomstore.h"

#include <QJsonValue>
#include <QLoggingCategory>

namespace {
Q_STATIC_LOGGING_CATEGORY(lcLoomStore, "loom.store")

// A value stored here outlives the scene that wrote it -- indefinitely, across
// any number of reloads -- so anything holding a pointer into that scene has to
// be refused at the write rather than left to dangle.
//
// The pointer check is not redundant with the JSON one. QJsonValue::fromVariant
// maps a QObject* to *Null*, not Undefined, so the JSON test alone accepts it
// and quietly stores a dangling pointer as null. statecapture.cpp:140 uses the
// JSON test on its own, which is survivable there because its envelope lives
// for exactly one reload; here it would not be.
bool isStorable(const QVariant &value)
{
    if (!value.isValid())
        return true; // clearing a key
    if (value.metaType().flags() & (QMetaType::PointerToQObject | QMetaType::IsPointer))
        return false;
    return !QJsonValue::fromVariant(value).isUndefined();
}
} // namespace

LoomStoreRegistry::LoomStoreRegistry(QObject *parent)
    : QObject(parent)
{
}

LoomStoreRegistry *LoomStoreRegistry::instance()
{
    static LoomStoreRegistry registry;
    return &registry;
}

QVariant LoomStoreRegistry::value(const QString &key) const
{
    return m_values.value(key);
}

bool LoomStoreRegistry::contains(const QString &key) const
{
    return m_values.contains(key);
}

QStringList LoomStoreRegistry::keys() const
{
    return m_values.keys();
}

QVariantMap LoomStoreRegistry::values() const
{
    return m_values;
}

bool LoomStoreRegistry::setValue(const QString &key, const QVariant &value)
{
    if (key.isEmpty())
        return false;
    if (!isStorable(value)) {
        qCWarning(lcLoomStore).noquote()
            << "Store:" << key << "was not stored -- only JSON-representable values "
                                  "survive a reload, and an object reference would "
                                  "dangle into the replaced scene";
        return false;
    }
    if (m_values.value(key) == value)
        return true;
    m_values.insert(key, value);
    emit valueChanged(key, value);
    return true;
}

void LoomStoreRegistry::clear()
{
    const QStringList cleared = m_values.keys();
    m_values.clear();
    for (const QString &key : cleared)
        emit valueChanged(key, QVariant());
}

LoomStore::LoomStore(QObject *parent)
    : QQmlPropertyMap(this, parent)
{
    auto *registry = LoomStoreRegistry::instance();
    // Seeded rather than starting empty: this facade is created fresh after
    // every clearSingletons(), and the whole point is that the scene finds the
    // values it left behind.
    m_seeding = true;
    const QVariantMap existing = registry->values();
    for (auto it = existing.constBegin(); it != existing.constEnd(); ++it)
        insert(it.key(), it.value());
    m_seeding = false;

    // A write from elsewhere in the process -- C++, or a second engine -- has
    // to reach this map too, or the two would drift apart.
    connect(
        registry, &LoomStoreRegistry::valueChanged, this,
        [this](const QString &key, const QVariant &value) {
            if (this->value(key) == value)
                return;
            m_seeding = true;
            insert(key, value);
            m_seeding = false;
        });
}

QVariant LoomStore::updateValue(const QString &key, const QVariant &input)
{
    if (m_seeding)
        return input;
    if (!LoomStoreRegistry::instance()->setValue(key, input)) {
        // Refused: keep whatever was already there rather than letting the map
        // hold a value the registry does not have.
        return value(key);
    }
    return input;
}
