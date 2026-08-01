#include "loomtokenregistry.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPointF>
#include <QStyleHints>
#include <algorithm>

#include "loomtokendata.h"

Q_STATIC_LOGGING_CATEGORY(lcLoomTokens, "loom.tokens")

namespace {

template <typename Themes, typename Select>
bool anyThemeContains(const Themes &themes, const QString &key, Select select)
{
    return std::any_of(themes.cbegin(), themes.cend(), [&](const auto &theme) {
        return select(theme).contains(key);
    });
}

template <typename Base, typename Themes, typename Select>
QStringList designWideKeys(const Base &base, const Themes &themes, Select select)
{
    QStringList keys = base.keys();
    for (auto theme = themes.cbegin(); theme != themes.cend(); ++theme)
        keys.append(select(*theme).keys());
    keys.sort();
    keys.removeDuplicates();
    return keys;
}

} // namespace

LoomTokenRegistry *LoomTokenRegistry::instance()
{
    static LoomTokenRegistry registry;
    return &registry;
}

LoomTokenRegistry::LoomTokenRegistry()
{
    seedDefaults();
    if (QGuiApplication::instance()) {
        connect(
            QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this] { refreshSystemTheme(); });
    }
}

void LoomTokenRegistry::resetToDefaults()
{
    // Deliberately a plain reset, including the active theme going back to
    // "light": a config's own themes do not exist again until it has been
    // re-applied, so preserving the active theme here could only ever fail.
    // The reload path in loomconfigloader.cpp restores it afterwards.
    m_colors.clear();
    m_themes.clear();
    m_space.clear();
    m_textSizes.clear();
    m_fontWeights.clear();
    m_fontFamilies.clear();
    m_tracking.clear();
    m_radius.clear();
    m_shadows.clear();
    m_opacity.clear();
    m_durations.clear();
    m_easing.clear();
    m_breakpoints.clear();
    m_containers.clear();
    m_styleRecipes.clear();
    m_arbitraryValuePolicy = QStringLiteral("warn");
    m_themeMode = ThemeMode::Explicit;
    m_systemLightTheme = QStringLiteral("light");
    m_systemDarkTheme = QStringLiteral("dark");

    seedDefaults();
}

// Every table in loomtokendata.h, expanded into the stores. Split out of the
// constructor so a config reload can start from the built-in set again: the
// config mutators only ever add, so without this a token deleted from a config
// file would survive the reload that removed it.
void LoomTokenRegistry::seedDefaults()
{
#define LOOM_FILL_COLOR(name, key, value)                                                \
    m_colors.insert(QStringLiteral(key), QColor::fromString(QLatin1String(value)));
    LOOM_PALETTE_COLORS(LOOM_FILL_COLOR)
#undef LOOM_FILL_COLOR

    Theme light;
    Theme dark;
    dark.dark = true;
#define LOOM_FILL_SEMANTIC(name, key, lightRef, darkRef)                                 \
    light.semantic.insert(                                                               \
        QStringLiteral(key), resolveColorRef(QStringLiteral(lightRef)));                 \
    dark.semantic.insert(QStringLiteral(key), resolveColorRef(QStringLiteral(darkRef)));
    LOOM_SEMANTIC_COLORS(LOOM_FILL_SEMANTIC)
#undef LOOM_FILL_SEMANTIC
    m_themes.insert(QStringLiteral("light"), light);
    m_themes.insert(QStringLiteral("dark"), dark);
    m_activeTheme = QStringLiteral("light");

#define LOOM_FILL_SPACE(name, key, px) m_space.insert(QStringLiteral(key), px);
    LOOM_SPACE_TOKENS(LOOM_FILL_SPACE)
#undef LOOM_FILL_SPACE

#define LOOM_FILL_TEXT_SIZE(name, key, size, lineHeight)                                 \
    m_textSizes.insert(QStringLiteral(key), LoomTextStyle{size, lineHeight});
    LOOM_TEXT_SIZES(LOOM_FILL_TEXT_SIZE)
#undef LOOM_FILL_TEXT_SIZE

#define LOOM_FILL_WEIGHT(name, key, weight)                                              \
    m_fontWeights.insert(QStringLiteral(key), weight);
    LOOM_FONT_WEIGHTS(LOOM_FILL_WEIGHT)
#undef LOOM_FILL_WEIGHT

    m_fontFamilies.insert(
        QStringLiteral("sans"), {QStringLiteral("Sans Serif"), QStringLiteral("Arial")});
    m_fontFamilies.insert(
        QStringLiteral("serif"), {QStringLiteral("Serif"), QStringLiteral("Times")});
    m_fontFamilies.insert(
        QStringLiteral("mono"),
        {QStringLiteral("Monospace"), QStringLiteral("Courier New")});

#define LOOM_FILL_TRACKING(name, key, em) m_tracking.insert(QStringLiteral(key), em);
    LOOM_TRACKING_TOKENS(LOOM_FILL_TRACKING)
#undef LOOM_FILL_TRACKING

#define LOOM_FILL_RADIUS(name, key, px) m_radius.insert(QStringLiteral(key), px);
    LOOM_RADIUS_TOKENS(LOOM_FILL_RADIUS)
#undef LOOM_FILL_RADIUS

#define LOOM_FILL_SHADOW(name, key, offsetY, blur, spread, alphaPercent)                 \
    m_shadows.insert(                                                                    \
        QStringLiteral(key),                                                             \
        LoomShadow{                                                                      \
            QColor(0, 0, 0, alphaPercent * 255 / 100), 0, offsetY, blur, spread});
    LOOM_SHADOW_TOKENS(LOOM_FILL_SHADOW)
#undef LOOM_FILL_SHADOW

#define LOOM_FILL_OPACITY(name, key, value) m_opacity.insert(QStringLiteral(key), value);
    LOOM_OPACITY_TOKENS(LOOM_FILL_OPACITY)
#undef LOOM_FILL_OPACITY

#define LOOM_FILL_DURATION(name, key, ms) m_durations.insert(QStringLiteral(key), ms);
    LOOM_DURATION_TOKENS(LOOM_FILL_DURATION)
#undef LOOM_FILL_DURATION

#define LOOM_FILL_EASING(name, key, x1, y1, x2, y2)                                      \
    {                                                                                    \
        QEasingCurve curve(QEasingCurve::BezierSpline);                                  \
        curve.addCubicBezierSegment(                                                     \
            QPointF(x1, y1), QPointF(x2, y2), QPointF(1.0, 1.0));                        \
        m_easing.insert(QStringLiteral(key), curve);                                     \
    }
    LOOM_EASING_TOKENS(LOOM_FILL_EASING)
#undef LOOM_FILL_EASING

#define LOOM_FILL_BREAKPOINT(name, key, px) m_breakpoints.insert(QStringLiteral(key), px);
    LOOM_BREAKPOINT_TOKENS(LOOM_FILL_BREAKPOINT)
#undef LOOM_FILL_BREAKPOINT

    const QList<std::pair<const char *, int>> containers{
        {"3xs", 256},  {"2xs", 288},  {"xs", 320},   {"sm", 384},  {"md", 448},
        {"lg", 512},   {"xl", 576},   {"2xl", 672},  {"3xl", 768}, {"4xl", 896},
        {"5xl", 1024}, {"6xl", 1152}, {"7xl", 1280},
    };
    for (const auto &[name, px] : containers)
        m_containers.insert(QLatin1String(name), px);
}

QColor LoomTokenRegistry::resolveColorRef(const QString &ref) const
{
    const auto paletteEntry = m_colors.constFind(ref);
    if (paletteEntry != m_colors.constEnd())
        return *paletteEntry;
    return QColor::fromString(ref);
}

QColor LoomTokenRegistry::resolveColorRef(const QString &ref, const Theme &scope) const
{
    // A semantic name the theme already carries -- its own, or inherited from
    // the base it extends -- can be aliased: {"accent-hover": "accent"}. Only
    // the palette was consulted before, so such an alias silently produced an
    // invalid QColor. Names defined in the same object are deliberately not
    // visible to each other: the resolution order would depend on hash order.
    const auto semanticEntry = scope.semantic.constFind(ref);
    if (semanticEntry != scope.semantic.constEnd())
        return *semanticEntry;
    return resolveColorRef(ref);
}

QColor LoomTokenRegistry::color(const QString &key) const
{
    const auto theme = m_themes.constFind(m_activeTheme);
    if (theme != m_themes.constEnd()) {
        const auto semanticEntry = theme->semantic.constFind(key);
        if (semanticEntry != theme->semantic.constEnd())
            return *semanticEntry;
    }
    return m_colors.value(key);
}

bool LoomTokenRegistry::hasColor(const QString &key) const
{
    const auto theme = m_themes.constFind(m_activeTheme);
    if (theme != m_themes.constEnd() && theme->semantic.contains(key))
        return true;
    return m_colors.contains(key);
}

bool LoomTokenRegistry::knowsColor(const QString &key) const
{
    return m_colors.contains(key)
        || anyThemeContains(m_themes, key, [](const auto &theme) -> const auto & {
               return theme.semantic;
           });
}

qreal LoomTokenRegistry::space(const QString &key) const
{
    if (const auto theme = m_themes.constFind(m_activeTheme);
        theme != m_themes.constEnd() && theme->overrides.space.contains(key))
        return theme->overrides.space.value(key);
    return m_space.value(key);
}

bool LoomTokenRegistry::hasSpace(const QString &key) const
{
    const auto theme = m_themes.constFind(m_activeTheme);
    return m_space.contains(key)
        || (theme != m_themes.constEnd() && theme->overrides.space.contains(key));
}

bool LoomTokenRegistry::knowsSpace(const QString &key) const
{
    return m_space.contains(key)
        || anyThemeContains(m_themes, key, [](const auto &theme) -> const auto & {
               return theme.overrides.space;
           });
}

LoomTextStyle LoomTokenRegistry::textSize(const QString &key) const
{
    if (const auto theme = m_themes.constFind(m_activeTheme);
        theme != m_themes.constEnd() && theme->overrides.textSizes.contains(key))
        return theme->overrides.textSizes.value(key);
    return m_textSizes.value(key);
}

bool LoomTokenRegistry::hasTextSize(const QString &key) const
{
    const auto theme = m_themes.constFind(m_activeTheme);
    return m_textSizes.contains(key)
        || (theme != m_themes.constEnd() && theme->overrides.textSizes.contains(key));
}

bool LoomTokenRegistry::knowsTextSize(const QString &key) const
{
    return m_textSizes.contains(key)
        || anyThemeContains(m_themes, key, [](const auto &theme) -> const auto & {
               return theme.overrides.textSizes;
           });
}

int LoomTokenRegistry::fontWeight(const QString &key) const
{
    if (const auto theme = m_themes.constFind(m_activeTheme);
        theme != m_themes.constEnd() && theme->overrides.fontWeights.contains(key))
        return theme->overrides.fontWeights.value(key);
    return m_fontWeights.value(key, 400);
}

bool LoomTokenRegistry::hasFontWeight(const QString &key) const
{
    const auto theme = m_themes.constFind(m_activeTheme);
    return m_fontWeights.contains(key)
        || (theme != m_themes.constEnd() && theme->overrides.fontWeights.contains(key));
}

bool LoomTokenRegistry::knowsFontWeight(const QString &key) const
{
    return m_fontWeights.contains(key)
        || anyThemeContains(m_themes, key, [](const auto &theme) -> const auto & {
               return theme.overrides.fontWeights;
           });
}

QStringList LoomTokenRegistry::fontFamily(const QString &key) const
{
    if (const auto theme = m_themes.constFind(m_activeTheme);
        theme != m_themes.constEnd() && theme->overrides.fontFamilies.contains(key))
        return theme->overrides.fontFamilies.value(key);
    return m_fontFamilies.value(key);
}

bool LoomTokenRegistry::hasFontFamily(const QString &key) const
{
    const auto theme = m_themes.constFind(m_activeTheme);
    return m_fontFamilies.contains(key)
        || (theme != m_themes.constEnd() && theme->overrides.fontFamilies.contains(key));
}

bool LoomTokenRegistry::knowsFontFamily(const QString &key) const
{
    return m_fontFamilies.contains(key)
        || anyThemeContains(m_themes, key, [](const auto &theme) -> const auto & {
               return theme.overrides.fontFamilies;
           });
}

qreal LoomTokenRegistry::tracking(const QString &key) const
{
    if (const auto theme = m_themes.constFind(m_activeTheme);
        theme != m_themes.constEnd() && theme->overrides.tracking.contains(key))
        return theme->overrides.tracking.value(key);
    return m_tracking.value(key);
}

bool LoomTokenRegistry::hasTracking(const QString &key) const
{
    const auto theme = m_themes.constFind(m_activeTheme);
    return m_tracking.contains(key)
        || (theme != m_themes.constEnd() && theme->overrides.tracking.contains(key));
}

bool LoomTokenRegistry::knowsTracking(const QString &key) const
{
    return m_tracking.contains(key)
        || anyThemeContains(m_themes, key, [](const auto &theme) -> const auto & {
               return theme.overrides.tracking;
           });
}

qreal LoomTokenRegistry::radius(const QString &key) const
{
    if (const auto theme = m_themes.constFind(m_activeTheme);
        theme != m_themes.constEnd() && theme->overrides.radius.contains(key))
        return theme->overrides.radius.value(key);
    return m_radius.value(key);
}

bool LoomTokenRegistry::hasRadius(const QString &key) const
{
    const auto theme = m_themes.constFind(m_activeTheme);
    return m_radius.contains(key)
        || (theme != m_themes.constEnd() && theme->overrides.radius.contains(key));
}

bool LoomTokenRegistry::knowsRadius(const QString &key) const
{
    return m_radius.contains(key)
        || anyThemeContains(m_themes, key, [](const auto &theme) -> const auto & {
               return theme.overrides.radius;
           });
}

LoomShadow LoomTokenRegistry::shadow(const QString &key) const
{
    if (const auto theme = m_themes.constFind(m_activeTheme);
        theme != m_themes.constEnd() && theme->overrides.shadows.contains(key))
        return theme->overrides.shadows.value(key);
    return m_shadows.value(key);
}

bool LoomTokenRegistry::hasShadow(const QString &key) const
{
    const auto theme = m_themes.constFind(m_activeTheme);
    return m_shadows.contains(key)
        || (theme != m_themes.constEnd() && theme->overrides.shadows.contains(key));
}

bool LoomTokenRegistry::knowsShadow(const QString &key) const
{
    return m_shadows.contains(key)
        || anyThemeContains(m_themes, key, [](const auto &theme) -> const auto & {
               return theme.overrides.shadows;
           });
}

qreal LoomTokenRegistry::opacityValue(const QString &key) const
{
    if (const auto theme = m_themes.constFind(m_activeTheme);
        theme != m_themes.constEnd() && theme->overrides.opacity.contains(key))
        return theme->overrides.opacity.value(key);
    return m_opacity.value(key, 1.0);
}

bool LoomTokenRegistry::hasOpacityValue(const QString &key) const
{
    const auto theme = m_themes.constFind(m_activeTheme);
    return m_opacity.contains(key)
        || (theme != m_themes.constEnd() && theme->overrides.opacity.contains(key));
}

bool LoomTokenRegistry::knowsOpacityValue(const QString &key) const
{
    return m_opacity.contains(key)
        || anyThemeContains(m_themes, key, [](const auto &theme) -> const auto & {
               return theme.overrides.opacity;
           });
}

int LoomTokenRegistry::duration(const QString &key) const
{
    if (const auto theme = m_themes.constFind(m_activeTheme);
        theme != m_themes.constEnd() && theme->overrides.durations.contains(key))
        return theme->overrides.durations.value(key);
    return m_durations.value(key);
}

bool LoomTokenRegistry::hasDuration(const QString &key) const
{
    const auto theme = m_themes.constFind(m_activeTheme);
    return m_durations.contains(key)
        || (theme != m_themes.constEnd() && theme->overrides.durations.contains(key));
}

bool LoomTokenRegistry::knowsDuration(const QString &key) const
{
    return m_durations.contains(key)
        || anyThemeContains(m_themes, key, [](const auto &theme) -> const auto & {
               return theme.overrides.durations;
           });
}

QEasingCurve LoomTokenRegistry::easing(const QString &key) const
{
    if (const auto theme = m_themes.constFind(m_activeTheme);
        theme != m_themes.constEnd() && theme->overrides.easing.contains(key))
        return theme->overrides.easing.value(key);
    return m_easing.value(key);
}

bool LoomTokenRegistry::hasEasing(const QString &key) const
{
    const auto theme = m_themes.constFind(m_activeTheme);
    return m_easing.contains(key)
        || (theme != m_themes.constEnd() && theme->overrides.easing.contains(key));
}

bool LoomTokenRegistry::knowsEasing(const QString &key) const
{
    return m_easing.contains(key)
        || anyThemeContains(m_themes, key, [](const auto &theme) -> const auto & {
               return theme.overrides.easing;
           });
}

int LoomTokenRegistry::breakpoint(const QString &key) const
{
    return m_breakpoints.value(key);
}

bool LoomTokenRegistry::hasBreakpoint(const QString &key) const
{
    return m_breakpoints.contains(key);
}

int LoomTokenRegistry::container(const QString &key) const
{
    return m_containers.value(key);
}

bool LoomTokenRegistry::hasContainer(const QString &key) const
{
    return m_containers.contains(key);
}

QString LoomTokenRegistry::theme() const
{
    return m_activeTheme;
}

bool LoomTokenRegistry::isDark() const
{
    return m_themes.value(m_activeTheme).dark;
}

void LoomTokenRegistry::setTheme(const QString &name)
{
    if (!m_themes.contains(name)) {
        qCWarning(lcLoomTokens).noquote()
            << "Loom.setTheme: unknown theme" << name
            << "- known themes:" << themeNames().join(QLatin1String(", "));
        return;
    }
    const bool modeChanged = m_themeMode != ThemeMode::Explicit;
    m_themeMode = ThemeMode::Explicit;
    if (name == m_activeTheme) {
        if (modeChanged)
            emit themeChanged();
        return;
    }
    m_activeTheme = name;
    emit themeChanged();
    emit tokensChanged();
}

LoomTokenRegistry::ThemeMode LoomTokenRegistry::themeMode() const
{
    return m_themeMode;
}

void LoomTokenRegistry::setThemeMode(ThemeMode mode)
{
    if (m_themeMode == mode && mode != ThemeMode::System)
        return;
    const bool modeChanged = m_themeMode != mode;
    const QString priorTheme = m_activeTheme;
    m_themeMode = mode;
    if (mode == ThemeMode::System)
        refreshSystemTheme();
    if (mode != ThemeMode::System || (modeChanged && priorTheme == m_activeTheme))
        emit themeChanged();
}

void LoomTokenRegistry::setSystemThemes(
    const QString &lightTheme, const QString &darkTheme)
{
    if (m_themes.contains(lightTheme))
        m_systemLightTheme = lightTheme;
    if (m_themes.contains(darkTheme))
        m_systemDarkTheme = darkTheme;
    if (m_themeMode == ThemeMode::System)
        refreshSystemTheme();
}

void LoomTokenRegistry::refreshSystemTheme()
{
    if (m_themeMode != ThemeMode::System)
        return;
    const bool dark = QGuiApplication::instance()
        && QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    const QString wanted = dark ? m_systemDarkTheme : m_systemLightTheme;
    if (!m_themes.contains(wanted) || wanted == m_activeTheme)
        return;
    m_activeTheme = wanted;
    emit themeChanged();
    emit tokensChanged();
}

void LoomTokenRegistry::addColor(const QString &key, const QColor &color)
{
    m_colors.insert(key, color);
}

void LoomTokenRegistry::addSpace(const QString &key, qreal px)
{
    m_space.insert(key, px);
}

void LoomTokenRegistry::addTextSize(const QString &key, const LoomTextStyle &style)
{
    m_textSizes.insert(key, style);
}

void LoomTokenRegistry::addFontWeight(const QString &key, int weight)
{
    m_fontWeights.insert(key, weight);
}

void LoomTokenRegistry::addFontFamily(const QString &key, const QStringList &families)
{
    m_fontFamilies.insert(key, families);
}

void LoomTokenRegistry::addTracking(const QString &key, qreal em)
{
    m_tracking.insert(key, em);
}

void LoomTokenRegistry::addRadius(const QString &key, qreal px)
{
    m_radius.insert(key, px);
}

void LoomTokenRegistry::addShadow(const QString &key, const LoomShadow &shadow)
{
    m_shadows.insert(key, shadow);
}

void LoomTokenRegistry::addOpacity(const QString &key, qreal value)
{
    m_opacity.insert(key, value);
}

void LoomTokenRegistry::addDuration(const QString &key, int milliseconds)
{
    m_durations.insert(key, milliseconds);
}

void LoomTokenRegistry::addEasing(const QString &key, const QEasingCurve &curve)
{
    m_easing.insert(key, curve);
}

bool LoomTokenRegistry::setBreakpoint(const QString &key, int px)
{
    if (key.isEmpty() || px <= 0)
        return false;
    m_breakpoints.insert(key, px);
    return true;
}

bool LoomTokenRegistry::setContainer(const QString &key, int px)
{
    if (key.isEmpty() || px <= 0)
        return false;
    m_containers.insert(key, px);
    return true;
}

void LoomTokenRegistry::setStyleRecipe(const QString &name, const QString &style)
{
    if (!name.isEmpty())
        m_styleRecipes.insert(name, style);
}

void LoomTokenRegistry::setArbitraryValuePolicy(const QString &policy)
{
    if (policy == QLatin1String("allow") || policy == QLatin1String("warn")
        || policy == QLatin1String("deny"))
        m_arbitraryValuePolicy = policy;
}

bool LoomTokenRegistry::defineTheme(
    const QString &name, const QString &base, const QHash<QString, QString> &semanticRefs,
    std::optional<bool> dark)
{
    auto existing = m_themes.find(name);
    if (existing == m_themes.end()) {
        Theme fresh;
        if (!base.isEmpty()) {
            const auto baseTheme = m_themes.constFind(base);
            if (baseTheme == m_themes.constEnd())
                return false;
            fresh = *baseTheme;
        }
        existing = m_themes.insert(name, fresh);
    }
    for (auto it = semanticRefs.constBegin(); it != semanticRefs.constEnd(); ++it) {
        const QColor resolved = resolveColorRef(it.value(), *existing);
        if (!resolved.isValid()) {
            qCWarning(lcLoomTokens).noquote()
                << "config: theme" << name << "gives" << it.key() << "the value"
                << it.value()
                << "which is neither a palette colour, a semantic name it "
                   "already has, nor a colour literal; ignored";
            continue;
        }
        existing->semantic.insert(it.key(), resolved);
    }
    if (dark.has_value())
        existing->dark = *dark;
    return true;
}

bool LoomTokenRegistry::defineTheme(
    const QString &name, const QString &base, const ThemeOverrides &overrides,
    std::optional<bool> dark)
{
    auto existing = m_themes.find(name);
    if (existing == m_themes.end()) {
        Theme fresh;
        if (!base.isEmpty()) {
            const auto baseTheme = m_themes.constFind(base);
            if (baseTheme == m_themes.constEnd())
                return false;
            fresh = *baseTheme;
        }
        existing = m_themes.insert(name, fresh);
    }
    for (auto it = overrides.colors.cbegin(); it != overrides.colors.cend(); ++it)
        existing->semantic.insert(it.key(), it.value());
    existing->overrides.space.insert(overrides.space);
    existing->overrides.textSizes.insert(overrides.textSizes);
    existing->overrides.fontWeights.insert(overrides.fontWeights);
    existing->overrides.fontFamilies.insert(overrides.fontFamilies);
    existing->overrides.tracking.insert(overrides.tracking);
    existing->overrides.radius.insert(overrides.radius);
    existing->overrides.shadows.insert(overrides.shadows);
    existing->overrides.opacity.insert(overrides.opacity);
    existing->overrides.durations.insert(overrides.durations);
    existing->overrides.easing.insert(overrides.easing);
    if (dark.has_value())
        existing->dark = *dark;
    return true;
}

void LoomTokenRegistry::announceConfigChange()
{
    // vocabularyChanged before tokensChanged: a config can introduce a token
    // name that did not exist, and a style string compiled before it did has
    // already dropped the rule that referenced it. Re-applying is not enough --
    // the string has to be compiled again -- so listeners recompile on this
    // signal and then apply on the next one.
    emit vocabularyChanged();
    emit tokensChanged();
}

QStringList LoomTokenRegistry::themeNames() const
{
    QStringList names = m_themes.keys();
    names.sort();
    return names;
}

namespace {

template <typename Hash> QStringList sortedKeys(const Hash &hash)
{
    QStringList keys = hash.keys();
    keys.sort();
    return keys;
}

} // namespace

QStringList LoomTokenRegistry::colorKeys() const
{
    return designWideKeys(m_colors, m_themes, [](const auto &theme) -> const auto & {
        return theme.semantic;
    });
}

QStringList LoomTokenRegistry::spaceKeys() const
{
    return designWideKeys(m_space, m_themes, [](const auto &theme) -> const auto & {
        return theme.overrides.space;
    });
}

QStringList LoomTokenRegistry::textSizeKeys() const
{
    return designWideKeys(m_textSizes, m_themes, [](const auto &theme) -> const auto & {
        return theme.overrides.textSizes;
    });
}

QStringList LoomTokenRegistry::fontWeightKeys() const
{
    return designWideKeys(m_fontWeights, m_themes, [](const auto &theme) -> const auto & {
        return theme.overrides.fontWeights;
    });
}

QStringList LoomTokenRegistry::fontFamilyKeys() const
{
    return designWideKeys(
        m_fontFamilies, m_themes,
        [](const auto &theme) -> const auto & { return theme.overrides.fontFamilies; });
}

QStringList LoomTokenRegistry::trackingKeys() const
{
    return designWideKeys(m_tracking, m_themes, [](const auto &theme) -> const auto & {
        return theme.overrides.tracking;
    });
}

QStringList LoomTokenRegistry::radiusKeys() const
{
    return designWideKeys(m_radius, m_themes, [](const auto &theme) -> const auto & {
        return theme.overrides.radius;
    });
}

QStringList LoomTokenRegistry::shadowKeys() const
{
    return designWideKeys(m_shadows, m_themes, [](const auto &theme) -> const auto & {
        return theme.overrides.shadows;
    });
}

QStringList LoomTokenRegistry::opacityKeys() const
{
    return designWideKeys(m_opacity, m_themes, [](const auto &theme) -> const auto & {
        return theme.overrides.opacity;
    });
}

QStringList LoomTokenRegistry::durationKeys() const
{
    return designWideKeys(m_durations, m_themes, [](const auto &theme) -> const auto & {
        return theme.overrides.durations;
    });
}

QStringList LoomTokenRegistry::easingKeys() const
{
    return designWideKeys(m_easing, m_themes, [](const auto &theme) -> const auto & {
        return theme.overrides.easing;
    });
}

QStringList LoomTokenRegistry::breakpointKeys() const
{
    QStringList keys = m_breakpoints.keys();
    std::sort(
        keys.begin(), keys.end(), [this](const QString &left, const QString &right) {
            const int leftValue = breakpoint(left);
            const int rightValue = breakpoint(right);
            return leftValue == rightValue ? left < right : leftValue < rightValue;
        });
    return keys;
}

QStringList LoomTokenRegistry::containerKeys() const
{
    QStringList keys = m_containers.keys();
    std::sort(
        keys.begin(), keys.end(), [this](const QString &left, const QString &right) {
            const int leftValue = container(left);
            const int rightValue = container(right);
            return leftValue == rightValue ? left < right : leftValue < rightValue;
        });
    return keys;
}

QString LoomTokenRegistry::styleRecipe(const QString &name) const
{
    return m_styleRecipes.value(name);
}

bool LoomTokenRegistry::hasStyleRecipe(const QString &name) const
{
    return m_styleRecipes.contains(name);
}

QStringList LoomTokenRegistry::styleRecipeKeys() const
{
    return sortedKeys(m_styleRecipes);
}

QString LoomTokenRegistry::arbitraryValuePolicy() const
{
    return m_arbitraryValuePolicy;
}

LoomTokenRegistry::MotionMode LoomTokenRegistry::motionMode() const
{
    return m_motionMode;
}

void LoomTokenRegistry::setMotionMode(MotionMode mode)
{
    if (m_motionMode == mode)
        return;
    m_motionMode = mode;
    emit accessibilityChanged();
}

bool LoomTokenRegistry::reduceMotion() const
{
    if (m_motionMode == MotionMode::Reduce)
        return true;
    if (m_motionMode == MotionMode::Full)
        return false;
    // Qt 6.11 does not expose a cross-platform reduced-motion style hint. The
    // environment hook gives desktop portals, launchers and test harnesses a
    // deterministic system-mode bridge until Qt grows one.
    return qEnvironmentVariableIntValue("LOOM_REDUCE_MOTION") == 1;
}
