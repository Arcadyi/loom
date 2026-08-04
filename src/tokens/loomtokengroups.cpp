#include "loomtokengroups.h"

#include <QMetaProperty>

namespace {

// `surface-alt` -> `surfaceAlt`, `brand-500` -> `brand500`, so a config-defined
// token can be reached with dotted access and not only through brackets. The
// transform documented in docs/styling/tokens.md, applied to the keys the
// X-macro tables never saw. Empty when the key has no dashes, because then the
// key already is the alias.
QString camelAlias(const QString &key)
{
    const QStringList parts = key.split(QLatin1Char('-'), Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return {};
    QString alias = parts.first();
    for (qsizetype i = 1; i < parts.size(); ++i) {
        QString part = parts.at(i);
        part[0] = part.at(0).toUpper();
        alias += part;
    }
    // A leading digit is not an identifier; the same `x` prefix the built-in
    // names use for `2xl`.
    if (!alias.isEmpty() && alias.at(0).isDigit())
        alias.prepend(QLatin1Char('x'));
    return alias;
}

// Adds or refreshes the entries for one scale. Built-in names are skipped
// against the *static* metaobject -- the compiled-in Q_PROPERTY set, before any
// dynamic entry -- because inserting over one of those would shadow the
// accessor that reads through the registry, which is the thing that makes
// theme switching work at all.
template <typename Group, typename Lookup>
void seedGroup(Group *group, const QStringList &keys, Lookup lookup)
{
    const QMetaObject &meta = Group::staticMetaObject;
    const auto add = [&](const QString &name, const QString &key) {
        if (name.isEmpty() || meta.indexOfProperty(name.toUtf8().constData()) >= 0)
            return;
        group->insert(name, QVariant::fromValue(lookup(key)));
    };
    for (const QString &key : keys) {
        add(key, key);
        add(camelAlias(key), key);
    }
}

template <typename Group, typename Keys, typename Lookup>
void wireToRegistry(Group *group, Keys keys, Lookup lookup)
{
    auto *const registry = LoomTokenRegistry::instance();
    const auto refresh = [group, keys, lookup]() { seedGroup(group, keys(), lookup); };
    refresh();
    QObject::connect(registry, &LoomTokenRegistry::tokensChanged, group, &Group::changed);
    // Values change on a theme switch; which keys *exist* changes on a config
    // load. Both have to re-seed, or a binding keeps the value it was given.
    QObject::connect(registry, &LoomTokenRegistry::tokensChanged, group, refresh);
    QObject::connect(registry, &LoomTokenRegistry::vocabularyChanged, group, refresh);
}

} // namespace

LoomColorGroup::LoomColorGroup(QObject *parent)
    : QQmlPropertyMap(this, parent)
{
    wireToRegistry(
        this, [] { return LoomTokenRegistry::instance()->colorKeys(); },
        [](const QString &key) { return LoomTokenRegistry::instance()->color(key); });
}

QColor LoomColorGroup::value(const QString &key) const
{
    return LoomTokenRegistry::instance()->color(key);
}

LoomSpaceGroup::LoomSpaceGroup(QObject *parent)
    : QQmlPropertyMap(this, parent)
{
    wireToRegistry(
        this, [] { return LoomTokenRegistry::instance()->spaceKeys(); },
        [](const QString &key) { return LoomTokenRegistry::instance()->space(key); });
}

qreal LoomSpaceGroup::value(const QString &key) const
{
    return LoomTokenRegistry::instance()->space(key);
}

LoomTextGroup::LoomTextGroup(QObject *parent)
    : QQmlPropertyMap(this, parent)
{
    wireToRegistry(
        this, [] { return LoomTokenRegistry::instance()->textSizeKeys(); },
        [](const QString &key) { return LoomTokenRegistry::instance()->textSize(key); });
}

LoomTextStyle LoomTextGroup::value(const QString &key) const
{
    return LoomTokenRegistry::instance()->textSize(key);
}

int LoomTextGroup::weight(const QString &key) const
{
    return LoomTokenRegistry::instance()->fontWeight(key);
}

qreal LoomTextGroup::tracking(const QString &key) const
{
    return LoomTokenRegistry::instance()->tracking(key);
}

qreal LoomRadiusGroup::value(const QString &key) const
{
    return LoomTokenRegistry::instance()->radius(key);
}

LoomRadiusGroup::LoomRadiusGroup(QObject *parent)
    : QQmlPropertyMap(this, parent)
{
    wireToRegistry(
        this, [] { return LoomTokenRegistry::instance()->radiusKeys(); },
        [](const QString &key) { return LoomTokenRegistry::instance()->radius(key); });
}

LoomFontGroup::LoomFontGroup(QObject *parent)
    : QQmlPropertyMap(this, parent)
{
    wireToRegistry(
        this, [] { return LoomTokenRegistry::instance()->fontWeightKeys(); },
        [](const QString &key) {
            return LoomTokenRegistry::instance()->fontWeight(key);
        });
}

QStringList LoomFontGroup::sans() const
{
    return value(QStringLiteral("sans"));
}

QStringList LoomFontGroup::serif() const
{
    return value(QStringLiteral("serif"));
}

QStringList LoomFontGroup::mono() const
{
    return value(QStringLiteral("mono"));
}

QStringList LoomFontGroup::value(const QString &key) const
{
    return LoomTokenRegistry::instance()->fontFamily(key);
}

LoomShadow LoomShadowGroup::value(const QString &key) const
{
    return LoomTokenRegistry::instance()->shadow(key);
}

qreal LoomOpacityGroup::value(const QString &key) const
{
    return LoomTokenRegistry::instance()->opacityValue(key);
}

int LoomDurationGroup::value(const QString &key) const
{
    return LoomTokenRegistry::instance()->duration(key);
}

QEasingCurve LoomEasingGroup::value(const QString &key) const
{
    return LoomTokenRegistry::instance()->easing(key);
}

int LoomBreakpointGroup::value(const QString &key) const
{
    return LoomTokenRegistry::instance()->breakpoint(key);
}

LoomShadowGroup::LoomShadowGroup(QObject *parent)
    : QQmlPropertyMap(this, parent)
{
    wireToRegistry(
        this, [] { return LoomTokenRegistry::instance()->shadowKeys(); },
        [](const QString &key) { return LoomTokenRegistry::instance()->shadow(key); });
}

LoomOpacityGroup::LoomOpacityGroup(QObject *parent)
    : QQmlPropertyMap(this, parent)
{
    wireToRegistry(
        this, [] { return LoomTokenRegistry::instance()->opacityKeys(); },
        [](const QString &key) {
            return LoomTokenRegistry::instance()->opacityValue(key);
        });
}

LoomDurationGroup::LoomDurationGroup(QObject *parent)
    : QQmlPropertyMap(this, parent)
{
    wireToRegistry(
        this, [] { return LoomTokenRegistry::instance()->durationKeys(); },
        [](const QString &key) { return LoomTokenRegistry::instance()->duration(key); });
}

LoomEasingGroup::LoomEasingGroup(QObject *parent)
    : QQmlPropertyMap(this, parent)
{
    wireToRegistry(
        this, [] { return LoomTokenRegistry::instance()->easingKeys(); },
        [](const QString &key) { return LoomTokenRegistry::instance()->easing(key); });
}

LoomBreakpointGroup::LoomBreakpointGroup(QObject *parent)
    : QQmlPropertyMap(this, parent)
{
    wireToRegistry(
        this, [] { return LoomTokenRegistry::instance()->breakpointKeys(); },
        [](const QString &key) {
            return LoomTokenRegistry::instance()->breakpoint(key);
        });
}
