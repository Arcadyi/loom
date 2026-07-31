#include "loomstylecompiler.h"

#include <QHash>
#include <QLoggingCategory>
#include <QMutex>
#include <QStringList>

#include "loomtokenregistry.h"

Q_STATIC_LOGGING_CATEGORY(lcLoomStyle, "loom.style")

namespace {

struct VariantSpec {
    quint8 minBreakpoint = 0;
    quint8 stateMask = 0;
};

// Namespace scope rather than function-local statics so the catalogue can
// enumerate the same tables the parser accepts; a variant added here shows up
// in completion data without a second list to keep in sync.
const QHash<QString, quint8> &breakpointVariants()
{
    static const QHash<QString, quint8> table = {
        {QStringLiteral("sm"), 1},
        {QStringLiteral("md"), 2},
        {QStringLiteral("lg"), 3},
        {QStringLiteral("xl"), 4},
    };
    return table;
}

const QHash<QString, quint8> &stateVariants()
{
    static const QHash<QString, quint8> table = {
        {QStringLiteral("hover"), LoomHoverState},
        {QStringLiteral("pressed"), LoomPressedState},
        {QStringLiteral("focus"), LoomFocusState},
        {QStringLiteral("disabled"), LoomDisabledState},
        {QStringLiteral("dark"), LoomDarkState},
    };
    return table;
}

// Returns false for unknown variant names.
bool parseVariant(QStringView name, VariantSpec *spec)
{
    const QHash<QString, quint8> &breakpoints = breakpointVariants();
    const QHash<QString, quint8> &states = stateVariants();
    const QString key = name.toString();
    if (const auto breakpoint = breakpoints.constFind(key);
        breakpoint != breakpoints.constEnd()) {
        // Two breakpoints on one class: the later one wins, like Tailwind's
        // last-declaration-wins, but it is almost certainly a typo.
        spec->minBreakpoint = *breakpoint;
        return true;
    }
    if (const auto state = states.constFind(key); state != states.constEnd()) {
        spec->stateMask |= *state;
        return true;
    }
    return false;
}

struct Parsed {
    // A single utility class can expand to several rules (p-4 sets four
    // paddings; rounded-t-lg sets two corners).
    QVarLengthArray<LoomStyleRule, 4> rules;
    bool ok = false;
};

void addRule(
    Parsed *out, LoomUtility utility, const QString &key = QString(), double literal = 0,
    bool flag = false)
{
    LoomStyleRule rule;
    rule.utility = utility;
    rule.key = key;
    rule.literal = literal;
    rule.flag = flag;
    out->rules.append(rule);
    out->ok = true;
}

void addSides(
    Parsed *out, const QString &key, LoomUtility top, LoomUtility right,
    LoomUtility bottom, LoomUtility left, QStringView sides)
{
    if (sides == QLatin1String("")) {
        addRule(out, top, key);
        addRule(out, right, key);
        addRule(out, bottom, key);
        addRule(out, left, key);
    } else if (sides == QLatin1String("x")) {
        addRule(out, right, key);
        addRule(out, left, key);
    } else if (sides == QLatin1String("y")) {
        addRule(out, top, key);
        addRule(out, bottom, key);
    } else if (sides == QLatin1String("t")) {
        addRule(out, top, key);
    } else if (sides == QLatin1String("r")) {
        addRule(out, right, key);
    } else if (sides == QLatin1String("b")) {
        addRule(out, bottom, key);
    } else if (sides == QLatin1String("l")) {
        addRule(out, left, key);
    }
}

// "p" family: sides suffix directly on the prefix letter (px-4, pt-2).
bool parseSpacingFamily(
    QStringView name, QChar prefix, LoomUtility top, LoomUtility right,
    LoomUtility bottom, LoomUtility left, Parsed *out)
{
    if (name.isEmpty() || name.front() != prefix)
        return false;
    QStringView rest = name.mid(1);
    QStringView sides;
    if (!rest.isEmpty() && !rest.startsWith(u'-')) {
        sides = rest.left(1);
        rest = rest.mid(1);
    }
    if (!rest.startsWith(u'-'))
        return false;
    const QString key = rest.mid(1).toString();
    if (!LoomTokenRegistry::instance()->hasSpace(key))
        return false;
    const auto before = out->rules.size();
    addSides(out, key, top, right, bottom, left, sides);
    return out->rules.size() > before;
}

void parseRounded(QStringView rest, Parsed *out)
{
    // rest is what follows "rounded": "", "-lg", "-t", "-t-lg", "-tl-full"...
    auto *registry = LoomTokenRegistry::instance();
    if (rest.isEmpty()) {
        addRule(out, LoomUtility::Radius, QStringLiteral("base"));
        return;
    }
    if (!rest.startsWith(u'-'))
        return;
    rest = rest.mid(1);

    static const struct {
        const char *name;
        std::initializer_list<LoomUtility> corners;
    } cornerSets[] = {
        {"tl", {LoomUtility::RadiusTopLeft}},
        {"tr", {LoomUtility::RadiusTopRight}},
        {"br", {LoomUtility::RadiusBottomRight}},
        {"bl", {LoomUtility::RadiusBottomLeft}},
        {"t", {LoomUtility::RadiusTopLeft, LoomUtility::RadiusTopRight}},
        {"b", {LoomUtility::RadiusBottomLeft, LoomUtility::RadiusBottomRight}},
        {"l", {LoomUtility::RadiusTopLeft, LoomUtility::RadiusBottomLeft}},
        {"r", {LoomUtility::RadiusTopRight, LoomUtility::RadiusBottomRight}},
    };
    for (const auto &cornerSet : cornerSets) {
        const QLatin1String setName(cornerSet.name);
        if (rest == setName
            || (rest.startsWith(setName) && rest[setName.size()] == u'-')) {
            QString key = QStringLiteral("base");
            if (rest != setName)
                key = rest.mid(setName.size() + 1).toString();
            if (!registry->hasRadius(key))
                return;
            for (LoomUtility corner : cornerSet.corners)
                addRule(out, corner, key);
            return;
        }
    }
    const QString key = rest.toString();
    if (registry->hasRadius(key))
        addRule(out, LoomUtility::Radius, key);
}

// Namespace scope for the same reason as the variant tables: the catalogue
// enumerates these rather than repeating them.
const QHash<QString, std::pair<LoomUtility, bool>> &exactUtilities()
{
    static const QHash<QString, std::pair<LoomUtility, bool>> table = {
        {QStringLiteral("visible"), {LoomUtility::Visible, true}},
        {QStringLiteral("invisible"), {LoomUtility::Visible, false}},
        {QStringLiteral("italic"), {LoomUtility::Italic, true}},
        {QStringLiteral("not-italic"), {LoomUtility::Italic, false}},
        {QStringLiteral("underline"), {LoomUtility::Underline, true}},
        {QStringLiteral("line-through"), {LoomUtility::LineThrough, true}},
        {QStringLiteral("no-underline"), {LoomUtility::Underline, false}},
        {QStringLiteral("w-full"), {LoomUtility::WidthFull, true}},
        {QStringLiteral("h-full"), {LoomUtility::HeightFull, true}},
    };
    return table;
}

const QHash<QString, QString> &easingUtilities()
{
    static const QHash<QString, QString> table = {
        {QStringLiteral("ease-linear"), QStringLiteral("linear")},
        {QStringLiteral("ease-in"), QStringLiteral("in")},
        {QStringLiteral("ease-out"), QStringLiteral("out")},
        {QStringLiteral("ease-in-out"), QStringLiteral("in-out")},
    };
    return table;
}

const QHash<QString, LoomTransitionMode> &transitionModeUtilities()
{
    static const QHash<QString, LoomTransitionMode> table = {
        {QStringLiteral("transition"), LoomTransitionMode::Default},
        {QStringLiteral("transition-all"), LoomTransitionMode::All},
        {QStringLiteral("transition-colors"), LoomTransitionMode::Colors},
        {QStringLiteral("transition-opacity"), LoomTransitionMode::Opacity},
        {QStringLiteral("transition-none"), LoomTransitionMode::None},
    };
    return table;
}

Parsed parseUtilityBase(QStringView name)
{
    Parsed out;
    auto *registry = LoomTokenRegistry::instance();

    // Exact-match utilities first.
    const QHash<QString, std::pair<LoomUtility, bool>> &exact = exactUtilities();
    if (const auto entry = exact.constFind(name.toString()); entry != exact.constEnd()) {
        addRule(&out, entry->first, QString(), 0, entry->second);
        return out;
    }

    const QHash<QString, LoomTransitionMode> &transitionModes = transitionModeUtilities();
    if (const auto mode = transitionModes.constFind(name.toString());
        mode != transitionModes.constEnd()) {
        addRule(&out, LoomUtility::TransitionMode, QString(), double(quint8(*mode)));
        return out;
    }

    if (name.startsWith(QLatin1String("duration-"))) {
        const QString rest = name.mid(qsizetype(qstrlen("duration-"))).toString();
        if (registry->hasDuration(rest))
            addRule(&out, LoomUtility::TransitionDuration, rest);
        return out;
    }

    const QHash<QString, QString> &easings = easingUtilities();
    if (const auto easing = easings.constFind(name.toString());
        easing != easings.constEnd()) {
        addRule(&out, LoomUtility::TransitionEase, *easing);
        return out;
    }

    if (name.startsWith(QLatin1String("rounded"))) {
        parseRounded(name.mid(qsizetype(qstrlen("rounded"))), &out);
        return out;
    }

    if (name == QLatin1String("border")) {
        addRule(&out, LoomUtility::BorderWidth, QString(), 1);
        return out;
    }
    if (name.startsWith(QLatin1String("border-"))) {
        const QString rest = name.mid(qsizetype(qstrlen("border-"))).toString();
        bool isNumber = false;
        const double width = rest.toDouble(&isNumber);
        if (isNumber)
            addRule(&out, LoomUtility::BorderWidth, QString(), width);
        else if (registry->hasColor(rest))
            addRule(&out, LoomUtility::BorderColor, rest);
        return out;
    }

    if (name.startsWith(QLatin1String("bg-"))) {
        const QString rest = name.mid(qsizetype(qstrlen("bg-"))).toString();
        if (registry->hasColor(rest))
            addRule(&out, LoomUtility::BgColor, rest);
        return out;
    }

    if (name.startsWith(QLatin1String("text-"))) {
        // Size keys win over color lookup, matching Tailwind.
        const QString rest = name.mid(qsizetype(qstrlen("text-"))).toString();
        if (registry->hasTextSize(rest))
            addRule(&out, LoomUtility::TextSize, rest);
        else if (registry->hasColor(rest))
            addRule(&out, LoomUtility::TextColor, rest);
        return out;
    }

    if (name.startsWith(QLatin1String("font-"))) {
        const QString rest = name.mid(qsizetype(qstrlen("font-"))).toString();
        if (registry->hasFontWeight(rest))
            addRule(&out, LoomUtility::FontWeight, rest);
        return out;
    }

    if (name.startsWith(QLatin1String("tracking-"))) {
        const QString rest = name.mid(qsizetype(qstrlen("tracking-"))).toString();
        if (registry->hasTracking(rest))
            addRule(&out, LoomUtility::Tracking, rest);
        return out;
    }

    if (name.startsWith(QLatin1String("opacity-"))) {
        const QString rest = name.mid(qsizetype(qstrlen("opacity-"))).toString();
        if (registry->hasOpacityValue(rest))
            addRule(&out, LoomUtility::Opacity, rest);
        return out;
    }

    if (name == QLatin1String("shadow")) {
        addRule(&out, LoomUtility::Shadow, QStringLiteral("base"));
        return out;
    }
    if (name.startsWith(QLatin1String("shadow-"))) {
        const QString rest = name.mid(qsizetype(qstrlen("shadow-"))).toString();
        if (registry->hasShadow(rest))
            addRule(&out, LoomUtility::Shadow, rest);
        return out;
    }

    if (name.startsWith(QLatin1String("gap-"))) {
        const QString rest = name.mid(qsizetype(qstrlen("gap-"))).toString();
        if (registry->hasSpace(rest))
            addRule(&out, LoomUtility::Gap, rest);
        return out;
    }

    if (name.startsWith(QLatin1String("w-"))) {
        const QString rest = name.mid(qsizetype(qstrlen("w-"))).toString();
        if (registry->hasSpace(rest))
            addRule(&out, LoomUtility::Width, rest);
        return out;
    }
    if (name.startsWith(QLatin1String("h-"))) {
        const QString rest = name.mid(qsizetype(qstrlen("h-"))).toString();
        if (registry->hasSpace(rest))
            addRule(&out, LoomUtility::Height, rest);
        return out;
    }
    if (name.startsWith(QLatin1String("size-"))) {
        const QString rest = name.mid(qsizetype(qstrlen("size-"))).toString();
        if (registry->hasSpace(rest)) {
            addRule(&out, LoomUtility::Width, rest);
            addRule(&out, LoomUtility::Height, rest);
        }
        return out;
    }

    if (parseSpacingFamily(
            name, u'p', LoomUtility::PaddingTop, LoomUtility::PaddingRight,
            LoomUtility::PaddingBottom, LoomUtility::PaddingLeft, &out))
        return out;
    if (parseSpacingFamily(
            name, u'm', LoomUtility::MarginTop, LoomUtility::MarginRight,
            LoomUtility::MarginBottom, LoomUtility::MarginLeft, &out))
        return out;

    return out;
}

} // namespace

namespace LoomStyleCompiler {

namespace {

QMutex g_cacheMutex;
QHash<QString, std::shared_ptr<const LoomCompiledStyle>> g_cache;

struct ClassParse {
    VariantSpec variant;
    Parsed parsed;
    quint8 variantCount = 0;
    bool ok = false;
};

// Only colours have an alpha channel to modify. Anywhere else a `/70` would be
// silently meaningless, which is worse than being told the class is unknown.
bool takesAlphaModifier(LoomUtility utility)
{
    switch (utility) {
    case LoomUtility::BgColor:
    case LoomUtility::TextColor:
    case LoomUtility::BorderColor:
        return true;
    default:
        return false;
    }
}

// Tailwind's colour-opacity modifier: `bg-surface/70`. Split off before the
// name reaches any utility matcher, so every colour family gets it without
// each one having to know it exists.
Parsed parseUtility(QStringView name)
{
    int alphaPercent = 100;
    const qsizetype slash = name.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0) {
        bool isNumber = false;
        const int parsed = name.mid(slash + 1).toInt(&isNumber);
        // Anything but a plain 0-100 leaves `out.ok` false, so the class warns
        // as unknown rather than quietly losing the modifier.
        if (!isNumber || parsed < 0 || parsed > 100)
            return {};
        alphaPercent = parsed;
        name = name.left(slash);
    }

    Parsed out = parseUtilityBase(name);
    if (alphaPercent == 100 || !out.ok)
        return out;

    for (LoomStyleRule &rule : out.rules) {
        if (!takesAlphaModifier(rule.utility))
            return {};
        rule.alphaPercent = static_cast<quint8>(alphaPercent);
    }
    return out;
}

// One class through the parser: variant prefixes, then the utility itself.
// Shared by compile() and unknownClasses() so a checker can never disagree with
// what actually applies.
ClassParse parseClass(const QString &klass)
{
    ClassParse out;
    const QStringList segments = klass.split(QLatin1Char(':'));
    out.variantCount = quint8(segments.size() - 1);
    for (qsizetype i = 0; i < segments.size() - 1; ++i) {
        if (!parseVariant(segments.at(i), &out.variant))
            return out;
    }
    out.parsed = parseUtility(segments.constLast());
    out.ok = out.parsed.ok;
    return out;
}

} // namespace

std::shared_ptr<const LoomCompiledStyle> compile(const QString &style)
{
    {
        QMutexLocker locker(&g_cacheMutex);
        if (const auto cached = g_cache.constFind(style); cached != g_cache.constEnd())
            return *cached;
    }

    auto compiled = std::make_shared<LoomCompiledStyle>();
    const QStringList classes = style.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &klass : classes) {
        const ClassParse parse = parseClass(klass);
        if (!parse.ok) {
            qCWarning(lcLoomStyle).noquote()
                << "Lo.style: unknown utility class" << klass << "in" << style;
            continue;
        }
        for (LoomStyleRule rule : parse.parsed.rules) {
            rule.minBreakpoint = parse.variant.minBreakpoint;
            rule.stateMask = parse.variant.stateMask;
            rule.variantCount = parse.variantCount;
            compiled->usedStates |= rule.stateMask;
            if (rule.minBreakpoint > 0)
                compiled->usesBreakpoints = true;
            if (rule.utility == LoomUtility::WidthFull
                || rule.utility == LoomUtility::HeightFull)
                compiled->usesParentSize = true;
            if (rule.utility == LoomUtility::MarginTop
                || rule.utility == LoomUtility::MarginRight
                || rule.utility == LoomUtility::MarginBottom
                || rule.utility == LoomUtility::MarginLeft)
                compiled->usesMargins = true;
            compiled->rules.append(rule);
        }
    }

    QMutexLocker locker(&g_cacheMutex);
    // Another thread may have compiled the same string meanwhile; keep the
    // first insertion so identical strings always share one instance.
    const auto cached = g_cache.constFind(style);
    if (cached != g_cache.constEnd())
        return *cached;
    g_cache.insert(style, compiled);
    return compiled;
}

void clearCache()
{
    QMutexLocker locker(&g_cacheMutex);
    g_cache.clear();
}

QStringList unknownClasses(const QString &style)
{
    QStringList unknown;
    const QStringList classes = style.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &klass : classes) {
        if (!parseClass(klass).ok)
            unknown.append(klass);
    }
    return unknown;
}

QStringList variantNames()
{
    QStringList names = breakpointVariants().keys();
    names.append(stateVariants().keys());
    names.sort();
    return names;
}

QStringList valuelessClasses()
{
    QStringList names = exactUtilities().keys();
    names.append(transitionModeUtilities().keys());
    names.append(easingUtilities().keys());
    // Both take an optional value, so the bare forms are valid classes too:
    // `rounded` is 4px and `border` is 1px.
    names.append(QStringLiteral("rounded"));
    names.append(QStringLiteral("border"));
    names.append(QStringLiteral("shadow"));
    names.sort();
    return names;
}

} // namespace LoomStyleCompiler
