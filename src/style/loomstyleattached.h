#pragma once

#include <QEasingCurve>
#include <QHash>
#include <QPointer>
#include <QQmlProperty>
#include <QQuickItem>
#include <QVariant>
#include <QVariantAnimation>
#include <QtQml/qqmlregistration.h>

#include "loomstylecompiler.h"

class LoomTargetProfile;

// Per-item styling engine behind `Lo.style`. Owns the compiled style, the
// interaction-state subscriptions, and the record of every property it has
// written (with the pre-Loom original values, so removing a class or clearing
// the style restores them). All (re)application is coalesced through the event
// loop: state flips, theme switches and window resizes each schedule one
// apply, and the apply diffs against the last written values so only changed
// properties are touched.
class LoomStyleAttached : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QString style READ style WRITE setStyle NOTIFY styleChanged)

public:
    explicit LoomStyleAttached(QObject *parent = nullptr);

    QString style() const;
    void setStyle(const QString &style);

signals:
    void styleChanged();

private slots:
    void scheduleApply();
    // Compiles m_style again, for when a config load changed which token names
    // exist rather than only what they resolve to.
    void recompile();

private:
    void applyNow();
    static bool
    transitionCovers(LoomTransitionMode mode, const QString &path, const QVariant &value);
    void stopAnimation(const QString &path);
    void animateWrite(
        const QQmlProperty &property, const QString &path, const QVariant &target,
        int durationMs, const QEasingCurve &curve);
    void syncShadow(const QString &shadowKey);
    void updateSubscriptions();
    void trackWindow();
    void trackParent();
    quint8 activeStates() const;
    QString marginPath(LoomUtility utility) const;
    QString backgroundPath(LoomUtility utility) const;
    int breakpointTier() const;
    bool connectPropertyNotify(QObject *sender, const char *propertyName);

    QQuickItem *m_target = nullptr;
    QString m_style;
    std::shared_ptr<const LoomCompiledStyle> m_compiled;

    QPointer<QQuickItem> m_watcher;    // hover/pressed handler host
    QPointer<QQuickItem> m_shadowItem; // managed RectangularShadow sibling
    bool m_shadowFailed = false;       // creation failed once; do not retry
    bool m_nativePressed = false;      // target has its own `pressed` property
    QMetaObject::Connection m_windowWidthConn;
    QMetaObject::Connection m_parentSizeConnW;
    QMetaObject::Connection m_parentSizeConnH;

    QHash<QString, QVariant> m_originals;
    QHash<QString, QVariant> m_lastWritten;
    QHash<QString, QPointer<QVariantAnimation>> m_animations;
    bool m_applyQueued = false;
};

// The attaching type: `Lo.style: "p-4 bg-surface"` on any Item.
class Lo : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo only provides the attached style property")
    QML_ATTACHED(LoomStyleAttached)

public:
    static LoomStyleAttached *qmlAttachedProperties(QObject *object);
};
