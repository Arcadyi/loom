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

LoomRadiusGroup::LoomRadiusGroup(QObject *parent)
    : QObject(parent)
{
    wireToRegistry(this);
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
