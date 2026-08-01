#pragma once

#include <QColor>
#include <QEasingCurve>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <optional>

#include "loomvaluetypes.h"

// Process-wide token store. Both API layers resolve through here: the typed
// groups' READ accessors and the attached-style apply pass. Values change only
// on theme switch or config load, both of which emit tokensChanged() so every
// consumer re-resolves.
class LoomTokenRegistry : public QObject {
    Q_OBJECT

public:
    static LoomTokenRegistry *instance();

    QColor color(const QString &key) const;
    bool hasColor(const QString &key) const;
    qreal space(const QString &key) const;
    bool hasSpace(const QString &key) const;
    LoomTextStyle textSize(const QString &key) const;
    bool hasTextSize(const QString &key) const;
    int fontWeight(const QString &key) const;
    bool hasFontWeight(const QString &key) const;
    qreal tracking(const QString &key) const;
    bool hasTracking(const QString &key) const;
    qreal radius(const QString &key) const;
    bool hasRadius(const QString &key) const;
    LoomShadow shadow(const QString &key) const;
    bool hasShadow(const QString &key) const;
    qreal opacityValue(const QString &key) const;
    bool hasOpacityValue(const QString &key) const;
    int duration(const QString &key) const;
    bool hasDuration(const QString &key) const;
    QEasingCurve easing(const QString &key) const;
    int breakpoint(const QString &key) const;

    QString theme() const;
    bool isDark() const;
    void setTheme(const QString &name);
    QStringList themeNames() const;

    // Sorted key enumeration, for tooling that needs the whole vocabulary
    // rather than a membership test (the style catalogue, completion data).
    // colorKeys() includes the active theme's semantic names alongside the
    // palette, matching what hasColor() accepts.
    QStringList colorKeys() const;
    QStringList spaceKeys() const;
    QStringList textSizeKeys() const;
    QStringList fontWeightKeys() const;
    QStringList trackingKeys() const;
    QStringList radiusKeys() const;
    QStringList shadowKeys() const;
    QStringList opacityKeys() const;
    QStringList durationKeys() const;
    QStringList easingKeys() const;
    QStringList breakpointKeys() const;

    // Config mutators (used by LoomConfigLoader). None of them emit change
    // signals; call announceConfigChange() once after a batch of updates.

    // Drops every token back to the built-in set from loomtokendata.h,
    // including the active theme going back to "light". The mutators below only
    // ever add, so reloading an edited config without this would keep tokens
    // the file no longer defines. The active theme is deliberately not
    // preserved here -- a config's own themes do not exist again until it has
    // been re-applied -- so the reload path restores it afterwards.
    void resetToDefaults();
    void addColor(const QString &key, const QColor &color);
    void addSpace(const QString &key, qreal px);
    bool setBreakpoint(const QString &key, int px);
    // Defines or extends a theme. `base` names the theme to copy from when
    // `name` does not exist yet (empty = start blank); `semanticRefs` values
    // are palette keys or color literals. Returns false when `base` is
    // unknown.
    bool defineTheme(
        const QString &name, const QString &base,
        const QHash<QString, QString> &semanticRefs, std::optional<bool> dark);
    void announceConfigChange();

    QColor resolveColorRef(const QString &ref) const;

signals:
    // Some token may resolve differently now (theme switch or config load).
    void tokensChanged();
    // The active theme name / darkness changed.
    void themeChanged();
    // The set of *known* token names changed, not just their values -- only a
    // config load does this. A style string compiled earlier dropped any rule
    // naming a token that did not exist yet, so re-applying it is not enough:
    // listeners have to compile the string again. Emitted just before
    // tokensChanged, so a recompile is always followed by an apply.
    void vocabularyChanged();

private:
    LoomTokenRegistry();
    void seedDefaults();

    struct Theme {
        QHash<QString, QColor> semantic;
        bool dark = false;
    };

    // As resolveColorRef(ref), but a semantic name already present in `scope`
    // (its own or inherited from the base it extends) resolves too, so one
    // theme colour can alias another.
    QColor resolveColorRef(const QString &ref, const Theme &scope) const;

    QHash<QString, QColor> m_colors;
    QHash<QString, Theme> m_themes;
    QString m_activeTheme;
    QHash<QString, qreal> m_space;
    QHash<QString, LoomTextStyle> m_textSizes;
    QHash<QString, int> m_fontWeights;
    QHash<QString, qreal> m_tracking;
    QHash<QString, qreal> m_radius;
    QHash<QString, LoomShadow> m_shadows;
    QHash<QString, qreal> m_opacity;
    QHash<QString, int> m_durations;
    QHash<QString, QEasingCurve> m_easing;
    QHash<QString, int> m_breakpoints;
};
