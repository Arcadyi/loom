#pragma once

#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// `Router` in QML: which route is current, and the history behind it.
//
//     Router.push("settings", { tab: "network" })
//     Router.back()
//
// A facade over LoomStoreRegistry, exactly like LoomStore, and for the same
// reason: a full reload calls clearSingletons(), so route state held by a QML
// singleton would be lost every time a file is saved -- which under `loom dev`
// means the application snapping back to its first page on every edit. Keeping
// it in the process-wide store instead makes navigation survive a reload
// outright, which is stronger than what statecapture could offer (it cannot see
// framework singletons at all).
//
// Storing under reserved `loom.route*` keys rather than in members keeps that
// survival property free and makes the state inspectable through Store.
class LoomRouter : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Router)
    QML_SINGLETON
    Q_PROPERTY(QString route READ route NOTIFY routeChanged)
    Q_PROPERTY(QVariantMap params READ params NOTIFY routeChanged)
    //! Every route pushed so far, oldest first, including the current one.
    Q_PROPERTY(QStringList stack READ stack NOTIFY routeChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY routeChanged)

public:
    explicit LoomRouter(QObject *parent = nullptr);

    QString route() const;
    QVariantMap params() const;
    QStringList stack() const;
    bool canGoBack() const;

    //! Navigates, keeping the current route on the stack.
    Q_INVOKABLE void push(const QString &route, const QVariantMap &params = {});
    //! Navigates, replacing the current route on the stack.
    Q_INVOKABLE void replace(const QString &route, const QVariantMap &params = {});
    //! Returns false when there was nothing to go back to.
    Q_INVOKABLE bool back();

signals:
    void routeChanged();
};
