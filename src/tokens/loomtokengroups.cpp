#include "loomtokengroups.h"

namespace {

template <typename Group> void wireToRegistry(Group *group)
{
    QObject::connect(
        LoomTokenRegistry::instance(), &LoomTokenRegistry::tokensChanged, group,
        &Group::changed);
}

} // namespace

LoomColorGroup::LoomColorGroup(QObject *parent)
    : QObject(parent)
{
    wireToRegistry(this);
}

QColor LoomColorGroup::value(const QString &key) const
{
    return LoomTokenRegistry::instance()->color(key);
}

LoomSpaceGroup::LoomSpaceGroup(QObject *parent)
    : QObject(parent)
{
    wireToRegistry(this);
}

qreal LoomSpaceGroup::value(const QString &key) const
{
    return LoomTokenRegistry::instance()->space(key);
}

LoomTextGroup::LoomTextGroup(QObject *parent)
    : QObject(parent)
{
    wireToRegistry(this);
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
    : QObject(parent)
{
    wireToRegistry(this);
}

LoomFontGroup::LoomFontGroup(QObject *parent)
    : QObject(parent)
{
    wireToRegistry(this);
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
    : QObject(parent)
{
    wireToRegistry(this);
}

LoomOpacityGroup::LoomOpacityGroup(QObject *parent)
    : QObject(parent)
{
    wireToRegistry(this);
}

LoomDurationGroup::LoomDurationGroup(QObject *parent)
    : QObject(parent)
{
    wireToRegistry(this);
}

LoomEasingGroup::LoomEasingGroup(QObject *parent)
    : QObject(parent)
{
    wireToRegistry(this);
}

LoomBreakpointGroup::LoomBreakpointGroup(QObject *parent)
    : QObject(parent)
{
    wireToRegistry(this);
}
