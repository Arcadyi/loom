#include "loomstylecompiler.h"

#include <QHash>
#include <QLoggingCategory>
#include <QMutex>
#include <QStringList>
#include <cmath>

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
        // Tailwind's spelling for "remove from layout". `invisible` is its
        // synonym here for now; in Tailwind proper it keeps the layout box
        // (`opacity: 0`), which Loom cannot express until opacity and the
        // visible flag stop sharing one utility. Documented in limitations.md.
        {QStringLiteral("hidden"), {LoomUtility::Visible, false}},
        {QStringLiteral("invisible"), {LoomUtility::Visible, false}},
        {QStringLiteral("italic"), {LoomUtility::Italic, true}},
        {QStringLiteral("not-italic"), {LoomUtility::Italic, false}},
        {QStringLiteral("underline"), {LoomUtility::Underline, true}},
        {QStringLiteral("line-through"), {LoomUtility::LineThrough, true}},
        {QStringLiteral("no-underline"), {LoomUtility::Underline, false}},
        {QStringLiteral("w-full"), {LoomUtility::WidthFull, true}},
        {QStringLiteral("h-full"), {LoomUtility::HeightFull, true}},
        // Layout. Valueless, so the catalogue enumerates them for free through
        // valuelessClasses().
        {QStringLiteral("fill"), {LoomUtility::AnchorFill, true}},
        {QStringLiteral("fill-x"), {LoomUtility::AnchorFillX, true}},
        {QStringLiteral("fill-y"), {LoomUtility::AnchorFillY, true}},
        {QStringLiteral("center"), {LoomUtility::AnchorCenter, true}},
        {QStringLiteral("center-x"), {LoomUtility::AnchorCenterX, true}},
        {QStringLiteral("center-y"), {LoomUtility::AnchorCenterY, true}},
        {QStringLiteral("pin-t"), {LoomUtility::AnchorPinTop, true}},
        {QStringLiteral("pin-r"), {LoomUtility::AnchorPinRight, true}},
        {QStringLiteral("pin-b"), {LoomUtility::AnchorPinBottom, true}},
        {QStringLiteral("pin-l"), {LoomUtility::AnchorPinLeft, true}},
    };
    return table;
}

// `self-*` writes the whole Qt::Alignment at once rather than composing flags.
// Two rules on one flags property would resolve by specificity instead of
// merging, so a decomposed `self-x`/`self-y` pair would silently lose an axis.
const QHash<QString, Qt::Alignment> &alignmentUtilities()
{
    static const QHash<QString, Qt::Alignment> table = {
        {QStringLiteral("self-start"), Qt::AlignLeft | Qt::AlignTop},
        {QStringLiteral("self-center"), Qt::AlignCenter},
        {QStringLiteral("self-end"), Qt::AlignRight | Qt::AlignBottom},
        // Qt's default is to stretch, which it spells as "no alignment".
        {QStringLiteral("self-stretch"), Qt::Alignment()},
    };
    return table;
}

// `aspect-{n}/{m}` is parsed here rather than as a named token because the
// ratio is a number, not a scale entry.
const QHash<QString, double> &aspectUtilities()
{
    static const QHash<QString, double> table = {
        {QStringLiteral("aspect-square"), 1.0},
        {QStringLiteral("aspect-video"), 16.0 / 9.0},
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
        // toDouble also accepts "nan", "inf" and negatives, none of which are a
        // border width. Reject them as unknown classes rather than writing
        // nonsense into the target's border.
        if (isNumber && std::isfinite(width) && width >= 0)
            addRule(&out, LoomUtility::BorderWidth, QString(), width);
        else if (!isNumber && registry->hasColor(rest))
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

    // Alignment, aspect ratio, size constraints and spans, all before the p/m
    // spacing families below: those match on a single leading character, so
    // anything starting with `p` or `m` has to be dispatched first.
    const QHash<QString, Qt::Alignment> &alignments = alignmentUtilities();
    if (const auto alignment = alignments.constFind(name.toString());
        alignment != alignments.constEnd()) {
        addRule(&out, LoomUtility::LayoutAlignment, QString(), double(*alignment));
        return out;
    }

    const QHash<QString, double> &aspects = aspectUtilities();
    if (const auto aspect = aspects.constFind(name.toString());
        aspect != aspects.constEnd()) {
        addRule(&out, LoomUtility::AspectRatio, QString(), *aspect);
        return out;
    }
    if (name.startsWith(QLatin1String("aspect-"))) {
        // `aspect-16/9`. Reaches here with the slash intact because parseUtility
        // tries the whole name before treating a trailing /N as a colour alpha.
        const QString rest = name.mid(qsizetype(qstrlen("aspect-"))).toString();
        const qsizetype slash = rest.indexOf(QLatin1Char('/'));
        if (slash > 0) {
            bool okWidth = false;
            bool okHeight = false;
            const double width = rest.left(slash).toDouble(&okWidth);
            const double height = rest.mid(slash + 1).toDouble(&okHeight);
            if (okWidth && okHeight && width > 0 && height > 0)
                addRule(&out, LoomUtility::AspectRatio, QString(), width / height);
        }
        return out;
    }

    struct ConstraintFamily {
        const char *prefix;
        LoomUtility utility;
    };
    static constexpr ConstraintFamily constraints[] = {
        {"min-w-", LoomUtility::LayoutMinWidth},
        {"max-w-", LoomUtility::LayoutMaxWidth},
        {"min-h-", LoomUtility::LayoutMinHeight},
        {"max-h-", LoomUtility::LayoutMaxHeight},
    };
    for (const ConstraintFamily &family : constraints) {
        if (!name.startsWith(QLatin1String(family.prefix)))
            continue;
        const QString rest = name.mid(qsizetype(qstrlen(family.prefix))).toString();
        if (registry->hasSpace(rest))
            addRule(&out, family.utility, rest);
        return out;
    }

    struct SpanFamily {
        const char *prefix;
        LoomUtility utility;
    };
    static constexpr SpanFamily spans[] = {
        {"col-span-", LoomUtility::LayoutColumnSpan},
        {"row-span-", LoomUtility::LayoutRowSpan},
    };
    for (const SpanFamily &family : spans) {
        if (!name.startsWith(QLatin1String(family.prefix)))
            continue;
        const QString rest = name.mid(qsizetype(qstrlen(family.prefix))).toString();
        bool isNumber = false;
        const int span = rest.toInt(&isNumber);
        // A span is a count of cells: at least one, and a whole number.
        if (isNumber && span >= 1)
            addRule(&out, family.utility, QString(), span);
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

const char *loomUtilityName(LoomUtility utility)
{
    switch (utility) {
    case LoomUtility::BgColor:
        return "bg-*";
    case LoomUtility::TextColor:
        return "text-{color}";
    case LoomUtility::TextSize:
        return "text-{size}";
    case LoomUtility::FontWeight:
        return "font-*";
    case LoomUtility::Italic:
        return "italic";
    case LoomUtility::Underline:
        return "underline";
    case LoomUtility::LineThrough:
        return "line-through";
    case LoomUtility::Tracking:
        return "tracking-*";
    case LoomUtility::PaddingTop:
    case LoomUtility::PaddingRight:
    case LoomUtility::PaddingBottom:
    case LoomUtility::PaddingLeft:
        return "p-*";
    case LoomUtility::MarginTop:
    case LoomUtility::MarginRight:
    case LoomUtility::MarginBottom:
    case LoomUtility::MarginLeft:
        return "m-*";
    case LoomUtility::Gap:
        return "gap-*";
    case LoomUtility::Width:
        return "w-*";
    case LoomUtility::Height:
        return "h-*";
    case LoomUtility::WidthFull:
        return "w-full";
    case LoomUtility::HeightFull:
        return "h-full";
    case LoomUtility::Radius:
    case LoomUtility::RadiusTopLeft:
    case LoomUtility::RadiusTopRight:
    case LoomUtility::RadiusBottomRight:
    case LoomUtility::RadiusBottomLeft:
        return "rounded-*";
    case LoomUtility::BorderWidth:
        return "border-{width}";
    case LoomUtility::BorderColor:
        return "border-{color}";
    case LoomUtility::Opacity:
        return "opacity-*";
    case LoomUtility::Visible:
        return "visible/hidden";
    case LoomUtility::AnchorFill:
        return "fill";
    case LoomUtility::AnchorFillX:
        return "fill-x";
    case LoomUtility::AnchorFillY:
        return "fill-y";
    case LoomUtility::AnchorCenter:
        return "center";
    case LoomUtility::AnchorCenterX:
        return "center-x";
    case LoomUtility::AnchorCenterY:
        return "center-y";
    case LoomUtility::AnchorPinTop:
        return "pin-t";
    case LoomUtility::AnchorPinRight:
        return "pin-r";
    case LoomUtility::AnchorPinBottom:
        return "pin-b";
    case LoomUtility::AnchorPinLeft:
        return "pin-l";
    case LoomUtility::LayoutAlignment:
        return "self-*";
    case LoomUtility::LayoutMinWidth:
        return "min-w-*";
    case LoomUtility::LayoutMaxWidth:
        return "max-w-*";
    case LoomUtility::LayoutMinHeight:
        return "min-h-*";
    case LoomUtility::LayoutMaxHeight:
        return "max-h-*";
    case LoomUtility::LayoutColumnSpan:
        return "col-span-*";
    case LoomUtility::LayoutRowSpan:
        return "row-span-*";
    case LoomUtility::AspectRatio:
        return "aspect-*";
    case LoomUtility::Shadow:
        return "shadow-*";
    case LoomUtility::TransitionMode:
        return "transition-*";
    case LoomUtility::TransitionDuration:
        return "duration-*";
    case LoomUtility::TransitionEase:
        return "ease-*";
    }
    return "?";
}

namespace LoomStyleCompiler {

namespace {

QMutex g_cacheMutex;
QHash<QString, std::shared_ptr<const LoomCompiledStyle>> g_cache;

struct ClassParse {
    VariantSpec variant;
    Parsed parsed;
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

// Tailwind's colour-opacity modifier: `bg-surface/70`. Handled here rather than
// in each colour family, so every one of them gets it for free.
//
// The whole name is tried first, and the slash is only treated as a modifier if
// that fails. Splitting first -- which is what this did originally -- meant a
// slash could never be part of a class: `aspect-16/9` parsed as `aspect-16`
// with alpha 9 and was rejected. Trying the base parse first also makes
// `w-1/2` report as an unknown class rather than being silently mangled into
// `w-1`, and leaves room for fractional widths later.
Parsed parseUtility(QStringView name)
{
    if (Parsed whole = parseUtilityBase(name); whole.ok)
        return whole;

    const qsizetype slash = name.lastIndexOf(QLatin1Char('/'));
    if (slash < 0)
        return {};

    bool isNumber = false;
    const int alphaPercent = name.mid(slash + 1).toInt(&isNumber);
    // Anything but a plain 0-100 leaves `out.ok` false, so the class warns as
    // unknown rather than quietly losing the modifier.
    if (!isNumber || alphaPercent < 0 || alphaPercent > 100)
        return {};

    Parsed out = parseUtilityBase(name.left(slash));
    if (!out.ok)
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
            rule.specificity =
                loomSpecificity(parse.variant.minBreakpoint, parse.variant.stateMask);
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
            // Everything from the layout block routes on the parent's type, so
            // a reparent has to re-apply. AspectRatio additionally tracks the
            // item's own width.
            if (rule.utility >= LoomUtility::AnchorFill
                && rule.utility <= LoomUtility::AspectRatio)
                compiled->usesLayout = true;
            if (rule.utility == LoomUtility::AspectRatio)
                compiled->usesAspect = true;
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
    names.append(alignmentUtilities().keys());
    // The named ratios only. `aspect-16/9` takes a value that is not a token,
    // so it is advertised through numericPrefixes instead.
    names.append(aspectUtilities().keys());
    // Both take an optional value, so the bare forms are valid classes too:
    // `rounded` is 4px and `border` is 1px.
    names.append(QStringLiteral("rounded"));
    names.append(QStringLiteral("border"));
    names.append(QStringLiteral("shadow"));
    names.sort();
    return names;
}

} // namespace LoomStyleCompiler
