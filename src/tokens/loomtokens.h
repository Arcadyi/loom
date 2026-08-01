#pragma once

#include <QColor>
#include <QObject>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "loomtokengroups.h"

// The `Loom` QML singleton. Named after the module on purpose (precedent:
// `Material` in QtQuick.Controls.Material): `import Loom` then
// `Loom.color.blue500`, `Loom.space.s4`, `Loom.theme = "dark"`.
class LoomTokens : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Loom)
    QML_SINGLETON
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(LoomColorGroup *color READ color CONSTANT)
    Q_PROPERTY(LoomSpaceGroup *space READ space CONSTANT)
    Q_PROPERTY(LoomTextGroup *text READ text CONSTANT)
    Q_PROPERTY(LoomFontGroup *font READ font CONSTANT)
    Q_PROPERTY(LoomRadiusGroup *radius READ radius CONSTANT)
    Q_PROPERTY(LoomShadowGroup *shadow READ shadow CONSTANT)
    Q_PROPERTY(LoomOpacityGroup *opacity READ opacity CONSTANT)
    Q_PROPERTY(LoomDurationGroup *duration READ duration CONSTANT)
    Q_PROPERTY(LoomEasingGroup *easing READ easing CONSTANT)
    Q_PROPERTY(LoomBreakpointGroup *breakpoint READ breakpoint CONSTANT)
    Q_PROPERTY(QUrl iconRoot READ iconRoot WRITE setIconRoot NOTIFY iconRootChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(bool dark READ isDark NOTIFY themeChanged)
    Q_PROPERTY(ThemeMode themeMode READ themeMode WRITE setThemeMode NOTIFY themeChanged)
    Q_PROPERTY(bool highContrast READ highContrast NOTIFY accessibilityChanged)
    Q_PROPERTY(
        MotionPreference motionPreference READ motionPreference WRITE setMotionPreference
            NOTIFY accessibilityChanged)
    Q_PROPERTY(bool reduceMotion READ reduceMotion NOTIFY accessibilityChanged)

public:
    enum ThemeMode { ExplicitTheme, SystemTheme };
    Q_ENUM(ThemeMode)
    enum MotionPreference { SystemMotion, ReduceMotion, FullMotion };
    Q_ENUM(MotionPreference)
    explicit LoomTokens(QObject *parent = nullptr);

    QString version() const;
    LoomColorGroup *color() const;
    LoomSpaceGroup *space() const;
    LoomTextGroup *text() const;
    LoomFontGroup *font() const;
    LoomRadiusGroup *radius() const;
    LoomShadowGroup *shadow() const;
    LoomOpacityGroup *opacity() const;
    LoomDurationGroup *duration() const;
    LoomEasingGroup *easing() const;
    LoomBreakpointGroup *breakpoint() const;

    // Directory that a relative icon() source resolves against. Set it
    // once, before the UI loads: icon() bindings track colors, not this,
    // so changing it later will not repaint icons already on screen.
    QUrl iconRoot() const;
    void setIconRoot(const QUrl &root);

    QString theme() const;
    void setTheme(const QString &name);
    bool isDark() const;
    ThemeMode themeMode() const;
    void setThemeMode(ThemeMode mode);
    bool highContrast() const;
    MotionPreference motionPreference() const;
    void setMotionPreference(MotionPreference preference);
    bool reduceMotion() const;

    Q_INVOKABLE QStringList themes() const;
    // Load a JSON config (see docs/styling/configuration.md). Accepts file: and qrc:
    // URLs; relative URLs resolve against the caller's QML file.
    Q_INVOKABLE bool loadConfig(const QUrl &url);
    // A URL that serves `source` repainted in `color`, for anything that takes
    // an image URL:
    //
    //     icon.source: Loom.icon("home.svg", Loom.color.foreground)
    //
    // Setting `icon.color` cannot do this -- Qt only tints an icon item that
    // is a mask, which a file source never is. See docs/styling/tokens.md.
    //
    // A relative `source` resolves against iconRoot, not against the calling
    // QML file: a singleton cannot see its caller, so per-file resolution is
    // not something this can offer honestly. Pass an absolute URL
    // (Qt.resolvedUrl("...")) to bypass the root. Pass a token rather than a
    // literal color to keep the binding live: it re-evaluates on theme switch
    // because `Loom.color.*` notifies. An invalid color serves the asset as
    // authored.
    Q_INVOKABLE QUrl icon(const QUrl &source, const QColor &color = {}) const;
    // Development-inspector data for an Item. Returns an empty map when the
    // object has no Lo attached state; it does not create one as a side effect.
    Q_INVOKABLE QVariantMap inspect(QObject *object) const;

signals:
    void themeChanged();
    void iconRootChanged();
    void accessibilityChanged();

private:
    LoomColorGroup *m_color;
    LoomSpaceGroup *m_space;
    LoomTextGroup *m_text;
    LoomFontGroup *m_font;
    LoomRadiusGroup *m_radius;
    LoomShadowGroup *m_shadow;
    LoomOpacityGroup *m_opacity;
    LoomDurationGroup *m_duration;
    LoomEasingGroup *m_easing;
    LoomBreakpointGroup *m_breakpoint;
};
