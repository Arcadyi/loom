#include "loomrouter.h"

#include "loomstore.h"

namespace {
// Reserved keys in the shared store. Namespaced so an application using Store
// for its own data cannot collide with them by accident.
const QString RouteKey = QStringLiteral("loom.route");
const QString ParamsKey = QStringLiteral("loom.route.params");
const QString StackKey = QStringLiteral("loom.route.stack");
} // namespace

LoomRouter::LoomRouter(QObject *parent)
    : QObject(parent)
{
    connect(
        LoomStoreRegistry::instance(), &LoomStoreRegistry::valueChanged, this,
        [this](const QString &key, const QVariant &) {
            if (key == RouteKey || key == ParamsKey || key == StackKey)
                emit routeChanged();
        });
}

QString LoomRouter::route() const
{
    return LoomStoreRegistry::instance()->value(RouteKey).toString();
}

QVariantMap LoomRouter::params() const
{
    return LoomStoreRegistry::instance()->value(ParamsKey).toMap();
}

QStringList LoomRouter::stack() const
{
    return LoomStoreRegistry::instance()->value(StackKey).toStringList();
}

bool LoomRouter::canGoBack() const
{
    return stack().size() > 1;
}

void LoomRouter::push(const QString &route, const QVariantMap &params)
{
    if (route.isEmpty())
        return;
    auto *store = LoomStoreRegistry::instance();
    QStringList history = stack();
    history.append(route);
    // Stack before route: a handler woken by routeChanged should see a stack
    // that already contains the route it is being told about.
    store->setValue(StackKey, history);
    store->setValue(ParamsKey, params);
    store->setValue(RouteKey, route);
}

void LoomRouter::replace(const QString &route, const QVariantMap &params)
{
    if (route.isEmpty())
        return;
    auto *store = LoomStoreRegistry::instance();
    QStringList history = stack();
    if (history.isEmpty())
        history.append(route);
    else
        history.last() = route;
    store->setValue(StackKey, history);
    store->setValue(ParamsKey, params);
    store->setValue(RouteKey, route);
}

bool LoomRouter::back()
{
    QStringList history = stack();
    if (history.size() < 2)
        return false;
    auto *store = LoomStoreRegistry::instance();
    history.removeLast();
    store->setValue(StackKey, history);
    // Parameters belong to the route that owned them, and going back does not
    // restore the previous route's parameters -- keeping a stack of those would
    // mean persisting arbitrary maps for the whole session. Applications that
    // need them should put them in the route name or in Store.
    store->setValue(ParamsKey, QVariantMap());
    store->setValue(RouteKey, history.last());
    return true;
}
