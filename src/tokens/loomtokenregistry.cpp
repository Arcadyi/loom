#include "loomtokenregistry.h"

#include <QLoggingCategory>
#include <QPointF>

#include "loomtokendata.h"

Q_STATIC_LOGGING_CATEGORY(lcLoomTokens, "loom.tokens")

LoomTokenRegistry *LoomTokenRegistry::instance()
{
    static LoomTokenRegistry registry;
    return &registry;
}

LoomTokenRegistry::LoomTokenRegistry()
{
    seedDefaults();
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
    m_tracking.clear();
    m_radius.clear();
    m_shadows.clear();
    m_opacity.clear();
    m_durations.clear();
    m_easing.clear();
    m_breakpoints.clear();

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

qreal LoomTokenRegistry::space(const QString &key) const
{
    return m_space.value(key);
}

bool LoomTokenRegistry::hasSpace(const QString &key) const
{
    return m_space.contains(key);
}

LoomTextStyle LoomTokenRegistry::textSize(const QString &key) const
{
    return m_textSizes.value(key);
}

bool LoomTokenRegistry::hasTextSize(const QString &key) const
{
    return m_textSizes.contains(key);
}

int LoomTokenRegistry::fontWeight(const QString &key) const
{
    return m_fontWeights.value(key, 400);
}

bool LoomTokenRegistry::hasFontWeight(const QString &key) const
{
    return m_fontWeights.contains(key);
}

qreal LoomTokenRegistry::tracking(const QString &key) const
{
    return m_tracking.value(key);
}

bool LoomTokenRegistry::hasTracking(const QString &key) const
{
    return m_tracking.contains(key);
}

qreal LoomTokenRegistry::radius(const QString &key) const
{
    return m_radius.value(key);
}

bool LoomTokenRegistry::hasRadius(const QString &key) const
{
    return m_radius.contains(key);
}

LoomShadow LoomTokenRegistry::shadow(const QString &key) const
{
    return m_shadows.value(key);
}

bool LoomTokenRegistry::hasShadow(const QString &key) const
{
    return m_shadows.contains(key);
}

qreal LoomTokenRegistry::opacityValue(const QString &key) const
{
    return m_opacity.value(key, 1.0);
}

bool LoomTokenRegistry::hasOpacityValue(const QString &key) const
{
    return m_opacity.contains(key);
}

int LoomTokenRegistry::duration(const QString &key) const
{
    return m_durations.value(key);
}

bool LoomTokenRegistry::hasDuration(const QString &key) const
{
    return m_durations.contains(key);
}

QEasingCurve LoomTokenRegistry::easing(const QString &key) const
{
    return m_easing.value(key);
}

int LoomTokenRegistry::breakpoint(const QString &key) const
{
    return m_breakpoints.value(key);
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
    if (name == m_activeTheme)
        return;
    if (!m_themes.contains(name)) {
        qCWarning(lcLoomTokens).noquote()
            << "Loom.setTheme: unknown theme" << name
            << "- known themes:" << themeNames().join(QLatin1String(", "));
        return;
    }
    m_activeTheme = name;
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

bool LoomTokenRegistry::setBreakpoint(const QString &key, int px)
{
    // The four tiers are structural (they map to the sm:..xl: prefixes);
    // configs may move the thresholds but not invent new tiers. A threshold at
    // or below zero is met by every window, which makes the tier meaningless
    // rather than merely unusual, so it is rejected outright.
    if (!m_breakpoints.contains(key) || px <= 0)
        return false;
    m_breakpoints.insert(key, px);
    return true;
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
    QStringList keys = m_colors.keys();
    // Semantic names live per theme rather than in the palette, and hasColor()
    // accepts them, so the enumeration has to as well or tooling would call
    // `bg-surface` unknown.
    if (const auto theme = m_themes.constFind(m_activeTheme);
        theme != m_themes.constEnd()) {
        for (auto it = theme->semantic.constBegin(); it != theme->semantic.constEnd();
             ++it) {
            if (!m_colors.contains(it.key()))
                keys.append(it.key());
        }
    }
    keys.sort();
    return keys;
}

QStringList LoomTokenRegistry::spaceKeys() const
{
    return sortedKeys(m_space);
}

QStringList LoomTokenRegistry::textSizeKeys() const
{
    return sortedKeys(m_textSizes);
}

QStringList LoomTokenRegistry::fontWeightKeys() const
{
    return sortedKeys(m_fontWeights);
}

QStringList LoomTokenRegistry::trackingKeys() const
{
    return sortedKeys(m_tracking);
}

QStringList LoomTokenRegistry::radiusKeys() const
{
    return sortedKeys(m_radius);
}

QStringList LoomTokenRegistry::shadowKeys() const
{
    return sortedKeys(m_shadows);
}

QStringList LoomTokenRegistry::opacityKeys() const
{
    return sortedKeys(m_opacity);
}

QStringList LoomTokenRegistry::durationKeys() const
{
    return sortedKeys(m_durations);
}

QStringList LoomTokenRegistry::easingKeys() const
{
    return sortedKeys(m_easing);
}

QStringList LoomTokenRegistry::breakpointKeys() const
{
    return sortedKeys(m_breakpoints);
}
