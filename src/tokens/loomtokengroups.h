#pragma once

#include <QColor>
#include <QEasingCurve>
#include <QObject>
#include <QtQml/qqmlregistration.h>

#include "loomtokendata.h"
#include "loomtokenregistry.h"
#include "loomvaluetypes.h"

// One QObject per token scale, exposed as grouped properties of the `Loom`
// singleton (`Loom.color.blue500`). Every property in a group shares the
// single changed() NOTIFY signal, wired to the registry's tokensChanged(), so
// a theme switch or config load re-evaluates every binding that goes through
// the group. READ accessors pull from the registry so overrides apply.

class LoomColorGroup : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

#define LOOM_PROP(name, key, ...) Q_PROPERTY(QColor name READ name NOTIFY changed)
    LOOM_PALETTE_COLORS(LOOM_PROP)
    LOOM_SEMANTIC_COLORS(LOOM_PROP)
#undef LOOM_PROP

public:
    explicit LoomColorGroup(QObject *parent = nullptr);

#define LOOM_READ(name, key, ...)                                                        \
    QColor name() const                                                                  \
    {                                                                                    \
        return LoomTokenRegistry::instance()->color(QStringLiteral(key));                \
    }
    LOOM_PALETTE_COLORS(LOOM_READ)
    LOOM_SEMANTIC_COLORS(LOOM_READ)
#undef LOOM_READ

    // Escape hatch for config-defined tokens ("brand-500"). Snapshot lookup:
    // a binding through it does not re-evaluate on theme switch.
    Q_INVOKABLE QColor value(const QString &key) const;

signals:
    void changed();
};

class LoomSpaceGroup : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

#define LOOM_PROP(name, key, ...) Q_PROPERTY(qreal name READ name NOTIFY changed)
    LOOM_SPACE_TOKENS(LOOM_PROP)
#undef LOOM_PROP

public:
    explicit LoomSpaceGroup(QObject *parent = nullptr);

#define LOOM_READ(name, key, ...)                                                        \
    qreal name() const                                                                   \
    {                                                                                    \
        return LoomTokenRegistry::instance()->space(QStringLiteral(key));                \
    }
    LOOM_SPACE_TOKENS(LOOM_READ)
#undef LOOM_READ

    Q_INVOKABLE qreal value(const QString &key) const;

signals:
    void changed();
};

class LoomTextGroup : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

#define LOOM_PROP(name, key, ...) Q_PROPERTY(LoomTextStyle name READ name NOTIFY changed)
    LOOM_TEXT_SIZES(LOOM_PROP)
#undef LOOM_PROP
#define LOOM_PROP(name, key, ...) Q_PROPERTY(int name READ name NOTIFY changed)
    LOOM_FONT_WEIGHTS(LOOM_PROP)
#undef LOOM_PROP
#define LOOM_PROP(name, key, ...) Q_PROPERTY(qreal name READ name NOTIFY changed)
    LOOM_TRACKING_TOKENS(LOOM_PROP)
#undef LOOM_PROP

public:
    explicit LoomTextGroup(QObject *parent = nullptr);

#define LOOM_READ(name, key, ...)                                                        \
    LoomTextStyle name() const                                                           \
    {                                                                                    \
        return LoomTokenRegistry::instance()->textSize(QStringLiteral(key));             \
    }
    LOOM_TEXT_SIZES(LOOM_READ)
#undef LOOM_READ
#define LOOM_READ(name, key, ...)                                                        \
    int name() const                                                                     \
    {                                                                                    \
        return LoomTokenRegistry::instance()->fontWeight(QStringLiteral(key));           \
    }
    LOOM_FONT_WEIGHTS(LOOM_READ)
#undef LOOM_READ
#define LOOM_READ(name, key, ...)                                                        \
    qreal name() const                                                                   \
    {                                                                                    \
        return LoomTokenRegistry::instance()->tracking(QStringLiteral(key));             \
    }
    LOOM_TRACKING_TOKENS(LOOM_READ)
#undef LOOM_READ

    // Escape hatch mirroring LoomColorGroup::value(), for size keys that are
    // only known at runtime ("2xl" from a model, config-defined entries).
    Q_INVOKABLE LoomTextStyle value(const QString &key) const;

signals:
    void changed();
};

class LoomRadiusGroup : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

#define LOOM_PROP(name, key, ...) Q_PROPERTY(qreal name READ name NOTIFY changed)
    LOOM_RADIUS_TOKENS(LOOM_PROP)
#undef LOOM_PROP

public:
    explicit LoomRadiusGroup(QObject *parent = nullptr);

#define LOOM_READ(name, key, ...)                                                        \
    qreal name() const                                                                   \
    {                                                                                    \
        return LoomTokenRegistry::instance()->radius(QStringLiteral(key));               \
    }
    LOOM_RADIUS_TOKENS(LOOM_READ)
#undef LOOM_READ

signals:
    void changed();
};

class LoomShadowGroup : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

#define LOOM_PROP(name, key, ...) Q_PROPERTY(LoomShadow name READ name NOTIFY changed)
    LOOM_SHADOW_TOKENS(LOOM_PROP)
#undef LOOM_PROP

public:
    explicit LoomShadowGroup(QObject *parent = nullptr);

#define LOOM_READ(name, key, ...)                                                        \
    LoomShadow name() const                                                              \
    {                                                                                    \
        return LoomTokenRegistry::instance()->shadow(QStringLiteral(key));               \
    }
    LOOM_SHADOW_TOKENS(LOOM_READ)
#undef LOOM_READ

signals:
    void changed();
};

class LoomOpacityGroup : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

#define LOOM_PROP(name, key, ...) Q_PROPERTY(qreal name READ name NOTIFY changed)
    LOOM_OPACITY_TOKENS(LOOM_PROP)
#undef LOOM_PROP

public:
    explicit LoomOpacityGroup(QObject *parent = nullptr);

#define LOOM_READ(name, key, ...)                                                        \
    qreal name() const                                                                   \
    {                                                                                    \
        return LoomTokenRegistry::instance()->opacityValue(QStringLiteral(key));         \
    }
    LOOM_OPACITY_TOKENS(LOOM_READ)
#undef LOOM_READ

signals:
    void changed();
};

class LoomDurationGroup : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

#define LOOM_PROP(name, key, ...) Q_PROPERTY(int name READ name NOTIFY changed)
    LOOM_DURATION_TOKENS(LOOM_PROP)
#undef LOOM_PROP

public:
    explicit LoomDurationGroup(QObject *parent = nullptr);

#define LOOM_READ(name, key, ...)                                                        \
    int name() const                                                                     \
    {                                                                                    \
        return LoomTokenRegistry::instance()->duration(QStringLiteral(key));             \
    }
    LOOM_DURATION_TOKENS(LOOM_READ)
#undef LOOM_READ

signals:
    void changed();
};

class LoomEasingGroup : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

#define LOOM_PROP(name, key, ...) Q_PROPERTY(QEasingCurve name READ name NOTIFY changed)
    LOOM_EASING_TOKENS(LOOM_PROP)
#undef LOOM_PROP

public:
    explicit LoomEasingGroup(QObject *parent = nullptr);

#define LOOM_READ(name, key, ...)                                                        \
    QEasingCurve name() const                                                            \
    {                                                                                    \
        return LoomTokenRegistry::instance()->easing(QStringLiteral(key));               \
    }
    LOOM_EASING_TOKENS(LOOM_READ)
#undef LOOM_READ

signals:
    void changed();
};

class LoomBreakpointGroup : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

#define LOOM_PROP(name, key, ...) Q_PROPERTY(int name READ name NOTIFY changed)
    LOOM_BREAKPOINT_TOKENS(LOOM_PROP)
#undef LOOM_PROP

public:
    explicit LoomBreakpointGroup(QObject *parent = nullptr);

#define LOOM_READ(name, key, ...)                                                        \
    int name() const                                                                     \
    {                                                                                    \
        return LoomTokenRegistry::instance()->breakpoint(QStringLiteral(key));           \
    }
    LOOM_BREAKPOINT_TOKENS(LOOM_READ)
#undef LOOM_READ

signals:
    void changed();
};
