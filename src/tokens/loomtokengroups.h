#pragma once

#include <QColor>
#include <QEasingCurve>
#include <QObject>
#include <QQmlPropertyMap>
#include <QtQml/qqmlregistration.h>

#include "loomtokendata.h"
#include "loomtokenregistry.h"
#include "loomvaluetypes.h"

// One group per token scale, exposed as grouped properties of the `Loom`
// singleton (`Loom.color.blue500`). Every property in a group shares the
// single changed() NOTIFY signal, wired to the registry's tokensChanged(), so
// a theme switch or config load re-evaluates every binding that goes through
// the group. READ accessors pull from the registry so overrides apply.
//
// QQmlPropertyMap rather than plain QObject so the *config-defined* tokens
// resolve too. The X-macro tables above generate a Q_PROPERTY per built-in
// token, and there is no such table for the ones a design file invents -- so
// `brand-500` had only Loom.color.value("brand-500"), a snapshot that never
// re-evaluated when the theme changed. Every scaffolded project defines a
// brand ramp, so every scaffolded project met that on day one.
//
// A property map gives per-key change notification that QML bindings actually
// track, which is the property value() lacks. It is the same choice LoomStore
// made, for the same reason. Custom keys are inserted in two spellings -- the
// registry's own (`Loom.color["brand-500"]`) and a camel alias for dotted
// access (`Loom.color.brand500`) -- and built-in names are never inserted,
// because those already exist as compiled-in properties.
//
// The map is writable from QML, as any QQmlPropertyMap is. Writing to one of
// these is not meaningful: the next theme switch or config load re-seeds every
// key from the registry and the write is gone. Tokens come from the design
// file.

class LoomColorGroup : public QQmlPropertyMap {
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

    // Direct registry lookup for a config-defined token. Kept for source
    // compatibility; prefer Loom.color["brand-500"], which re-resolves when
    // the theme changes where this does not.
    Q_INVOKABLE QColor value(const QString &key) const;

signals:
    void changed();
};

class LoomSpaceGroup : public QQmlPropertyMap {
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

class LoomTextGroup : public QQmlPropertyMap {
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
    Q_INVOKABLE int weight(const QString &key) const;
    Q_INVOKABLE qreal tracking(const QString &key) const;

signals:
    void changed();
};

class LoomRadiusGroup : public QQmlPropertyMap {
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

    Q_INVOKABLE qreal value(const QString &key) const;

signals:
    void changed();
};

class LoomFontGroup : public QQmlPropertyMap {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QStringList sans READ sans NOTIFY changed)
    Q_PROPERTY(QStringList serif READ serif NOTIFY changed)
    Q_PROPERTY(QStringList mono READ mono NOTIFY changed)

public:
    explicit LoomFontGroup(QObject *parent = nullptr);
    QStringList sans() const;
    QStringList serif() const;
    QStringList mono() const;
    Q_INVOKABLE QStringList value(const QString &key) const;

signals:
    void changed();
};

class LoomShadowGroup : public QQmlPropertyMap {
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

    Q_INVOKABLE LoomShadow value(const QString &key) const;

signals:
    void changed();
};

class LoomOpacityGroup : public QQmlPropertyMap {
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

    Q_INVOKABLE qreal value(const QString &key) const;

signals:
    void changed();
};

class LoomDurationGroup : public QQmlPropertyMap {
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

    Q_INVOKABLE int value(const QString &key) const;

signals:
    void changed();
};

class LoomEasingGroup : public QQmlPropertyMap {
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

    Q_INVOKABLE QEasingCurve value(const QString &key) const;

signals:
    void changed();
};

class LoomBreakpointGroup : public QQmlPropertyMap {
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

    Q_INVOKABLE int value(const QString &key) const;

signals:
    void changed();
};
