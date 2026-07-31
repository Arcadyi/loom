#pragma once

#include <QColor>
#include <QObject>
#include <QtQml/qqmlregistration.h>

// Composite token value types. Anonymous: they are never declared in QML, only
// returned from token properties (`Loom.text.xl.size`, `Loom.shadow.md.blur`).
struct LoomTextStyle {
    Q_GADGET
    QML_ANONYMOUS
    Q_PROPERTY(qreal size MEMBER size)
    Q_PROPERTY(qreal lineHeight MEMBER lineHeight)

public:
    qreal size = 0;
    qreal lineHeight = 0;

    bool operator==(const LoomTextStyle &other) const = default;
};

struct LoomShadow {
    Q_GADGET
    QML_ANONYMOUS
    Q_PROPERTY(QColor color MEMBER color)
    Q_PROPERTY(qreal offsetX MEMBER offsetX)
    Q_PROPERTY(qreal offsetY MEMBER offsetY)
    Q_PROPERTY(qreal blur MEMBER blur)
    Q_PROPERTY(qreal spread MEMBER spread)

public:
    QColor color;
    qreal offsetX = 0;
    qreal offsetY = 0;
    qreal blur = 0;
    qreal spread = 0;

    bool operator==(const LoomShadow &other) const = default;
};
