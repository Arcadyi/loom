#pragma once

#include <QColor>
#include <QEasingCurve>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariant>
#include <optional>

#include "loomvaluetypes.h"

// Process-wide token store. Both API layers resolve through here: the typed
// groups' READ accessors and the attached-style apply pass. Values change only
// on theme switch or config load, both of which emit tokensChanged() so every
// consumer re-resolves.
class LoomTokenRegistry : public QObject {
    Q_OBJECT

public:
    enum class ThemeMode { Explicit, System };
    enum class MotionMode { System, Reduce, Full };
    static LoomTokenRegistry *instance();

    QColor color(const QString &key) const;
    bool hasColor(const QString &key) const;
    bool knowsColor(const QString &key) const;
    qreal space(const QString &key) const;
    bool hasSpace(const QString &key) const;
    bool knowsSpace(const QString &key) const;
    LoomTextStyle textSize(const QString &key) const;
    bool hasTextSize(const QString &key) const;
    bool knowsTextSize(const QString &key) const;
    int fontWeight(const QString &key) const;
    bool hasFontWeight(const QString &key) const;
    bool knowsFontWeight(const QString &key) const;
    QStringList fontFamily(const QString &key) const;
    bool hasFontFamily(const QString &key) const;
    bool knowsFontFamily(const QString &key) const;
    qreal tracking(const QString &key) const;
    bool hasTracking(const QString &key) const;
    bool knowsTracking(const QString &key) const;
    qreal radius(const QString &key) const;
    bool hasRadius(const QString &key) const;
    bool knowsRadius(const QString &key) const;
    LoomShadow shadow(const QString &key) const;
    bool hasShadow(const QString &key) const;
    bool knowsShadow(const QString &key) const;
    qreal opacityValue(const QString &key) const;
    bool hasOpacityValue(const QString &key) const;
    bool knowsOpacityValue(const QString &key) const;
    int duration(const QString &key) const;
    bool hasDuration(const QString &key) const;
    bool knowsDuration(const QString &key) const;
    QEasingCurve easing(const QString &key) const;
    bool hasEasing(const QString &key) const;
    bool knowsEasing(const QString &key) const;
    int breakpoint(const QString &key) const;
    bool hasBreakpoint(const QString &key) const;
    int container(const QString &key) const;
    bool hasContainer(const QString &key) const;

    QString theme() const;
    bool isDark() const;
    void setTheme(const QString &name);
    ThemeMode themeMode() const;
    void setThemeMode(ThemeMode mode);
    void setSystemThemes(const QString &lightTheme, const QString &darkTheme);
    void refreshSystemTheme();
    QStringList themeNames() const;

    // Sorted key enumeration, for tooling that needs the whole vocabulary
    // rather than a membership test (the style catalogue, completion data).
    // The key lists include names introduced by every configured theme. This
    // is the design-wide vocabulary used by the compiler and tooling; has*()
    // remains the active-theme membership test used while applying a rule.
    QStringList colorKeys() const;
    QStringList spaceKeys() const;
    QStringList textSizeKeys() const;
    QStringList fontWeightKeys() const;
    QStringList fontFamilyKeys() const;
    QStringList trackingKeys() const;
    QStringList radiusKeys() const;
    QStringList shadowKeys() const;
    QStringList opacityKeys() const;
    QStringList durationKeys() const;
    QStringList easingKeys() const;
    QStringList breakpointKeys() const;
    QStringList containerKeys() const;
    QString styleRecipe(const QString &name) const;
    bool hasStyleRecipe(const QString &name) const;
    QStringList styleRecipeKeys() const;
    QString arbitraryValuePolicy() const;
    MotionMode motionMode() const;
    void setMotionMode(MotionMode mode);
    bool reduceMotion() const;

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
    void addTextSize(const QString &key, const LoomTextStyle &style);
    void addFontWeight(const QString &key, int weight);
    void addFontFamily(const QString &key, const QStringList &families);
    void addTracking(const QString &key, qreal em);
    void addRadius(const QString &key, qreal px);
    void addShadow(const QString &key, const LoomShadow &shadow);
    void addOpacity(const QString &key, qreal value);
    void addDuration(const QString &key, int milliseconds);
    void addEasing(const QString &key, const QEasingCurve &curve);
    bool setBreakpoint(const QString &key, int px);
    bool setContainer(const QString &key, int px);
    void setStyleRecipe(const QString &name, const QString &style);
    void setArbitraryValuePolicy(const QString &policy);

    struct ThemeOverrides {
        QHash<QString, QColor> colors;
        QHash<QString, qreal> space;
        QHash<QString, LoomTextStyle> textSizes;
        QHash<QString, int> fontWeights;
        QHash<QString, QStringList> fontFamilies;
        QHash<QString, qreal> tracking;
        QHash<QString, qreal> radius;
        QHash<QString, LoomShadow> shadows;
        QHash<QString, qreal> opacity;
        QHash<QString, int> durations;
        QHash<QString, QEasingCurve> easing;
    };
    // Defines or extends a theme. `base` names the theme to copy from when
    // `name` does not exist yet (empty = start blank); `semanticRefs` values
    // are palette keys or color literals. Returns false when `base` is
    // unknown.
    bool defineTheme(
        const QString &name, const QString &base,
        const QHash<QString, QString> &semanticRefs, std::optional<bool> dark);
    bool defineTheme(
        const QString &name, const QString &base, const ThemeOverrides &overrides,
        std::optional<bool> dark);
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
    void accessibilityChanged();

private:
    LoomTokenRegistry();
    void seedDefaults();

    struct Theme {
        QHash<QString, QColor> semantic;
        ThemeOverrides overrides;
        bool dark = false;
    };

    // As resolveColorRef(ref), but a semantic name already present in `scope`
    // (its own or inherited from the base it extends) resolves too, so one
    // theme colour can alias another.
    QColor resolveColorRef(const QString &ref, const Theme &scope) const;

    QHash<QString, QColor> m_colors;
    QHash<QString, Theme> m_themes;
    QString m_activeTheme;
    ThemeMode m_themeMode = ThemeMode::Explicit;
    QString m_systemLightTheme = QStringLiteral("light");
    QString m_systemDarkTheme = QStringLiteral("dark");
    QHash<QString, qreal> m_space;
    QHash<QString, LoomTextStyle> m_textSizes;
    QHash<QString, int> m_fontWeights;
    QHash<QString, QStringList> m_fontFamilies;
    QHash<QString, qreal> m_tracking;
    QHash<QString, qreal> m_radius;
    QHash<QString, LoomShadow> m_shadows;
    QHash<QString, qreal> m_opacity;
    QHash<QString, int> m_durations;
    QHash<QString, QEasingCurve> m_easing;
    QHash<QString, int> m_breakpoints;
    QHash<QString, int> m_containers;
    QHash<QString, QString> m_styleRecipes;
    QString m_arbitraryValuePolicy = QStringLiteral("warn");
    MotionMode m_motionMode = MotionMode::System;
};
