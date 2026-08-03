#include "loomstylecompiler.h"

#include <QFont>
#include <QHash>
#include <QLoggingCategory>
#include <QMutex>
#include <QRegularExpression>
#include <QStringList>
#include <cmath>
#include <functional>
#include <optional>

#include "loomtokenregistry.h"

Q_STATIC_LOGGING_CATEGORY(lcLoomStyle, "loom.style")

namespace {

struct VariantSpec {
    quint8 minBreakpoint = 0;
    quint32 stateMask = 0;
    quint32 stateNotMask = 0;
    int minWidth = 0;
    int maxWidth = std::numeric_limits<int>::max();
    int containerMinWidth = 0;
    int containerMaxWidth = std::numeric_limits<int>::max();
    QString containerName;
    quint32 groupStateMask = 0;
    quint32 groupStateNotMask = 0;
    quint32 customMask = 0;
    quint32 customNotMask = 0;
    quint32 groupCustomMask = 0;
    quint32 groupCustomNotMask = 0;
    QString groupName;
    QString themeName;
};

// A declared application state, as a mask bit. Looked up only after the
// built-in table has been asked, so a config can never shadow `hover` -- the
// loader rejects such a name outright, and this ordering means even a registry
// that somehow held one could not change what `hover:` compiles to.
std::optional<quint32> customStateMask(const QString &name)
{
    const int bit = LoomTokenRegistry::instance()->customStateBit(name);
    if (bit < 0)
        return std::nullopt;
    return quint32(1) << bit;
}

const QHash<QString, quint32> &stateVariants()
{
    static const QHash<QString, quint32> table = {
        {QStringLiteral("hover"), LoomHoverState},
        {QStringLiteral("pressed"), LoomPressedState},
        {QStringLiteral("focus"), LoomFocusState},
        {QStringLiteral("disabled"), LoomDisabledState},
        {QStringLiteral("dark"), LoomDarkState},
        {QStringLiteral("checked"), LoomCheckedState},
        {QStringLiteral("down"), LoomDownState},
        {QStringLiteral("highlighted"), LoomHighlightedState},
        {QStringLiteral("selected"), LoomSelectedState},
        {QStringLiteral("editable"), LoomEditableState},
        {QStringLiteral("read-only"), LoomReadOnlyState},
        {QStringLiteral("active"), LoomActiveState},
        {QStringLiteral("focus-within"), LoomFocusWithinState},
        {QStringLiteral("focus-visible"), LoomFocusVisibleState},
        {QStringLiteral("rtl"), LoomRtlState},
        {QStringLiteral("ltr"), LoomLtrState},
        {QStringLiteral("portrait"), LoomPortraitState},
        {QStringLiteral("landscape"), LoomLandscapeState},
        {QStringLiteral("window-active"), LoomWindowActiveState},
        {QStringLiteral("high-contrast"), LoomHighContrastState},
        {QStringLiteral("motion-reduce"), LoomMotionReduceState},
        {QStringLiteral("first"), LoomFirstState},
        {QStringLiteral("last"), LoomLastState},
        {QStringLiteral("only"), LoomOnlyState},
        {QStringLiteral("odd"), LoomOddState},
        {QStringLiteral("even"), LoomEvenState},
    };
    return table;
}

std::optional<double> bracketNumber(QStringView value);

// Returns false for unknown variant names.
bool parseVariant(QStringView name, VariantSpec *spec)
{
    const QHash<QString, quint32> &states = stateVariants();
    const QString key = name.toString();
    auto *registry = LoomTokenRegistry::instance();
    const auto arbitraryWidth = [](QStringView candidate, QStringView prefix) {
        if (!candidate.startsWith(prefix))
            return std::optional<int>();
        const auto number = bracketNumber(candidate.mid(prefix.size()));
        if (!number || *number <= 0 || *number > std::numeric_limits<int>::max())
            return std::optional<int>();
        return std::optional<int>(int(*number));
    };
    if (key.startsWith(QLatin1Char('@'))) {
        QString query = key.mid(1);
        const qsizetype slash = query.indexOf(QLatin1Char('/'));
        if (slash >= 0) {
            spec->containerName = query.mid(slash + 1);
            query = query.left(slash);
        }
        if (registry->hasContainer(query)) {
            spec->containerMinWidth = registry->container(query);
            return true;
        }
        if (query.startsWith(QLatin1String("max-"))) {
            const QString size = query.mid(qstrlen("max-"));
            if (registry->hasContainer(size)) {
                spec->containerMaxWidth = registry->container(size) - 1;
                return true;
            }
        }
        if (const auto minimum = arbitraryWidth(query, u"min-")) {
            spec->containerMinWidth = *minimum;
            return true;
        }
        if (const auto maximum = arbitraryWidth(query, u"max-")) {
            spec->containerMaxWidth = *maximum - 1;
            return true;
        }
        return false;
    }
    if (key.startsWith(QLatin1String("group-"))) {
        QString stateName = key.mid(qstrlen("group-"));
        const qsizetype slash = stateName.indexOf(QLatin1Char('/'));
        if (slash >= 0) {
            spec->groupName = stateName.mid(slash + 1);
            stateName = stateName.left(slash);
        }
        if (stateName.startsWith(QLatin1String("not-"))) {
            const QString positive = stateName.mid(qstrlen("not-"));
            const auto state = states.constFind(positive);
            if (state != states.constEnd()) {
                spec->groupStateNotMask |= *state;
                return true;
            }
            if (const auto custom = customStateMask(positive)) {
                spec->groupCustomNotMask |= *custom;
                return true;
            }
        } else if (
            const auto state = states.constFind(stateName); state != states.constEnd()) {
            spec->groupStateMask |= *state;
            return true;
        } else if (const auto custom = customStateMask(stateName)) {
            spec->groupCustomMask |= *custom;
            return true;
        }
        return false;
    }
    if (key.startsWith(QLatin1String("theme-"))) {
        const QString theme = key.mid(qstrlen("theme-"));
        if (registry->themeNames().contains(theme)) {
            spec->themeName = theme;
            return true;
        }
    }
    if (registry->hasBreakpoint(key)) {
        const int index = registry->breakpointKeys().indexOf(key);
        spec->minBreakpoint = quint8(std::clamp(index + 1, 1, 7));
        spec->minWidth = registry->breakpoint(key);
        return true;
    }
    if (key.startsWith(QLatin1String("max-"))) {
        const QString breakpoint = key.mid(qstrlen("max-"));
        if (registry->hasBreakpoint(breakpoint)) {
            spec->maxWidth = registry->breakpoint(breakpoint) - 1;
            spec->minBreakpoint = 1;
            return true;
        }
    }
    if (const auto minimum = arbitraryWidth(name, u"min-")) {
        spec->minWidth = *minimum;
        spec->minBreakpoint = 1;
        return true;
    }
    if (const auto maximum = arbitraryWidth(name, u"max-")) {
        spec->maxWidth = *maximum - 1;
        spec->minBreakpoint = 1;
        return true;
    }
    if (const auto state = states.constFind(key); state != states.constEnd()) {
        spec->stateMask |= *state;
        return true;
    }
    if (const auto custom = customStateMask(key)) {
        spec->customMask |= *custom;
        return true;
    }
    if (key.startsWith(QLatin1String("not-"))) {
        const QString positive = key.mid(qstrlen("not-"));
        if (const auto state = states.constFind(positive); state != states.constEnd()) {
            spec->stateNotMask |= *state;
            return true;
        }
        if (const auto custom = customStateMask(positive)) {
            spec->customNotMask |= *custom;
            return true;
        }
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

std::optional<double> bracketNumber(QStringView value)
{
    if (value.size() < 3 || value.front() != u'[' || value.back() != u']')
        return std::nullopt;
    QString text = value.mid(1, value.size() - 2).toString().trimmed();
    if (text.endsWith(QLatin1String("px")))
        text.chop(2);
    bool ok = false;
    const double number = text.toDouble(&ok);
    if (!ok || !std::isfinite(number))
        return std::nullopt;
    return number;
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
    const auto arbitrary = bracketNumber(key);
    if (!LoomTokenRegistry::instance()->knowsSpace(key) && !arbitrary)
        return false;
    const auto before = out->rules.size();
    addSides(out, key, top, right, bottom, left, sides);
    if (arbitrary) {
        for (qsizetype i = before; i < out->rules.size(); ++i) {
            out->rules[i].literal = *arbitrary;
            out->rules[i].arbitrary = true;
        }
    }
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
            const auto arbitrary = bracketNumber(key);
            if (!registry->knowsRadius(key) && (!arbitrary || *arbitrary < 0))
                return;
            for (LoomUtility corner : cornerSet.corners) {
                addRule(out, corner, key);
                if (arbitrary) {
                    out->rules.last().literal = *arbitrary;
                    out->rules.last().arbitrary = true;
                }
            }
            return;
        }
    }
    const QString key = rest.toString();
    if (registry->knowsRadius(key)) {
        addRule(out, LoomUtility::Radius, key);
    } else if (const auto arbitrary = bracketNumber(key); arbitrary && *arbitrary >= 0) {
        addRule(out, LoomUtility::Radius, key, *arbitrary);
        out->rules.last().arbitrary = true;
    }
}

// Namespace scope for the same reason as the variant tables: the catalogue
// enumerates these rather than repeating them.
const QHash<QString, std::pair<LoomUtility, bool>> &exactUtilities()
{
    static const QHash<QString, std::pair<LoomUtility, bool>> table = {
        {QStringLiteral("visible"), {LoomUtility::Visible, true}},
        // Tailwind's spelling for removing an item from layout. `invisible`
        // is parsed separately as opacity zero so it keeps its layout slot.
        {QStringLiteral("hidden"), {LoomUtility::Visible, false}},
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
        {QStringLiteral("overflow-hidden"), {LoomUtility::Clip, true}},
        {QStringLiteral("overflow-visible"), {LoomUtility::Clip, false}},
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

const QHash<QString, Qt::CursorShape> &cursorUtilities()
{
    static const QHash<QString, Qt::CursorShape> table = {
        {QStringLiteral("cursor-auto"), Qt::ArrowCursor},
        {QStringLiteral("cursor-default"), Qt::ArrowCursor},
        {QStringLiteral("cursor-pointer"), Qt::PointingHandCursor},
        {QStringLiteral("cursor-text"), Qt::IBeamCursor},
        {QStringLiteral("cursor-wait"), Qt::WaitCursor},
        {QStringLiteral("cursor-progress"), Qt::BusyCursor},
        {QStringLiteral("cursor-crosshair"), Qt::CrossCursor},
        {QStringLiteral("cursor-not-allowed"), Qt::ForbiddenCursor},
        {QStringLiteral("cursor-grab"), Qt::OpenHandCursor},
        {QStringLiteral("cursor-grabbing"), Qt::ClosedHandCursor},
        {QStringLiteral("cursor-move"), Qt::SizeAllCursor},
        {QStringLiteral("cursor-ew-resize"), Qt::SizeHorCursor},
        {QStringLiteral("cursor-ns-resize"), Qt::SizeVerCursor},
        {QStringLiteral("cursor-help"), Qt::WhatsThisCursor},
    };
    return table;
}

const QHash<QString, int> &gradientDirections()
{
    // Clockwise from top. The runtime preserves all eight values even though
    // Rectangle.Gradient can only render the nearest horizontal/vertical axis.
    static const QHash<QString, int> table = {
        {QStringLiteral("bg-linear-to-t"), 0}, {QStringLiteral("bg-linear-to-tr"), 1},
        {QStringLiteral("bg-linear-to-r"), 2}, {QStringLiteral("bg-linear-to-br"), 3},
        {QStringLiteral("bg-linear-to-b"), 4}, {QStringLiteral("bg-linear-to-bl"), 5},
        {QStringLiteral("bg-linear-to-l"), 6}, {QStringLiteral("bg-linear-to-tl"), 7},
    };
    return table;
}

Parsed parseUtilityBase(QStringView name)
{
    Parsed out;
    auto *registry = LoomTokenRegistry::instance();

    if (name.startsWith(u'-')) {
        Parsed negative = parseUtilityBase(name.mid(1));
        if (!negative.ok)
            return negative;
        for (auto &rule : negative.rules) {
            if (rule.utility != LoomUtility::MarginTop
                && rule.utility != LoomUtility::MarginRight
                && rule.utility != LoomUtility::MarginBottom
                && rule.utility != LoomUtility::MarginLeft
                && rule.utility != LoomUtility::TranslateX
                && rule.utility != LoomUtility::TranslateY
                && rule.utility != LoomUtility::Rotation
                && rule.utility != LoomUtility::ZOrder)
                return {};
            rule.negative = true;
        }
        return negative;
    }

    if (name == QLatin1String("invisible")) {
        addRule(&out, LoomUtility::Opacity, QString(), 0);
        out.rules.last().arbitrary = true;
        return out;
    }

    static const QHash<QString, int> textAlignments{
        {QStringLiteral("text-left"), int(Qt::AlignLeft)},
        {QStringLiteral("text-center"), int(Qt::AlignHCenter)},
        {QStringLiteral("text-right"), int(Qt::AlignRight)},
        {QStringLiteral("text-justify"), int(Qt::AlignJustify)},
    };
    if (const auto value = textAlignments.constFind(name.toString());
        value != textAlignments.constEnd()) {
        addRule(&out, LoomUtility::TextAlignment, QString(), *value);
        return out;
    }
    static const QHash<QString, int> textElides{
        {QStringLiteral("text-ellipsis"), int(Qt::ElideRight)},
        {QStringLiteral("text-clip"), int(Qt::ElideNone)},
    };
    if (name == QLatin1String("truncate")) {
        addRule(&out, LoomUtility::TextElide, QString(), int(Qt::ElideRight));
        addRule(&out, LoomUtility::TextWrapMode, QString(), 0);
        return out;
    }
    if (const auto value = textElides.constFind(name.toString());
        value != textElides.constEnd()) {
        addRule(&out, LoomUtility::TextElide, QString(), *value);
        return out;
    }
    if (name.startsWith(QLatin1String("line-clamp-"))) {
        const QString value = name.mid(qstrlen("line-clamp-")).toString();
        if (value == QLatin1String("none"))
            addRule(
                &out, LoomUtility::TextMaximumLines, QString(),
                std::numeric_limits<int>::max());
        else {
            bool ok = false;
            const int lines = value.toInt(&ok);
            if (ok && lines > 0)
                addRule(&out, LoomUtility::TextMaximumLines, QString(), lines);
        }
        return out;
    }
    static const QHash<QString, int> capitalization{
        {QStringLiteral("normal-case"), int(QFont::MixedCase)},
        {QStringLiteral("uppercase"), int(QFont::AllUppercase)},
        {QStringLiteral("lowercase"), int(QFont::AllLowercase)},
        {QStringLiteral("capitalize"), int(QFont::Capitalize)},
    };
    if (const auto value = capitalization.constFind(name.toString());
        value != capitalization.constEnd()) {
        addRule(&out, LoomUtility::TextCapitalization, QString(), *value);
        return out;
    }
    static const QHash<QString, int> wrapping{
        {QStringLiteral("whitespace-nowrap"), 0},
        {QStringLiteral("whitespace-normal"), 1},
        {QStringLiteral("wrap-anywhere"), 2},
    };
    if (const auto value = wrapping.constFind(name.toString());
        value != wrapping.constEnd()) {
        addRule(&out, LoomUtility::TextWrapMode, QString(), *value);
        return out;
    }
    if (name.startsWith(QLatin1String("z-"))) {
        const QStringView value = name.mid(qstrlen("z-"));
        bool ok = false;
        double number = value.toDouble(&ok);
        if (const auto arbitrary = bracketNumber(value)) {
            number = *arbitrary;
            ok = true;
        }
        if (ok) {
            addRule(&out, LoomUtility::ZOrder, QString(), number);
            out.rules.last().arbitrary = value.startsWith(u'[');
        }
        return out;
    }
    if (name.startsWith(QLatin1String("rotate-"))) {
        const QStringView value = name.mid(qstrlen("rotate-"));
        bool ok = false;
        double degrees = value.toDouble(&ok);
        if (const auto arbitrary = bracketNumber(value)) {
            degrees = *arbitrary;
            ok = true;
        }
        if (ok) {
            addRule(&out, LoomUtility::Rotation, QString(), degrees);
            out.rules.last().arbitrary = value.startsWith(u'[');
        }
        return out;
    }
    if (name.startsWith(QLatin1String("scale-"))) {
        const QStringView value = name.mid(qstrlen("scale-"));
        bool ok = false;
        double percent = value.toDouble(&ok);
        if (const auto arbitrary = bracketNumber(value)) {
            percent = *arbitrary;
            ok = true;
        }
        if (ok && percent >= 0) {
            addRule(&out, LoomUtility::Scale, QString(), percent / 100.0);
            out.rules.last().arbitrary = value.startsWith(u'[');
        }
        return out;
    }
    static const QHash<QString, int> origins{
        {QStringLiteral("origin-top-left"), 0},     {QStringLiteral("origin-top"), 1},
        {QStringLiteral("origin-top-right"), 2},    {QStringLiteral("origin-left"), 3},
        {QStringLiteral("origin-center"), 4},       {QStringLiteral("origin-right"), 5},
        {QStringLiteral("origin-bottom-left"), 6},  {QStringLiteral("origin-bottom"), 7},
        {QStringLiteral("origin-bottom-right"), 8},
    };
    if (const auto value = origins.constFind(name.toString());
        value != origins.constEnd()) {
        addRule(&out, LoomUtility::TransformOrigin, QString(), *value);
        return out;
    }
    if (const auto value = cursorUtilities().constFind(name.toString());
        value != cursorUtilities().constEnd()) {
        addRule(&out, LoomUtility::CursorShape, QString(), int(*value));
        return out;
    }

    for (const auto &[prefix, utility] : {
             std::pair{QLatin1StringView("translate-x-"), LoomUtility::TranslateX},
             std::pair{QLatin1StringView("translate-y-"), LoomUtility::TranslateY},
         }) {
        if (!name.startsWith(prefix))
            continue;
        const QString rest = name.mid(prefix.size()).toString();
        if (registry->knowsSpace(rest)) {
            addRule(&out, utility, rest);
        } else if (const auto arbitrary = bracketNumber(rest)) {
            addRule(&out, utility, QString(), *arbitrary);
            out.rules.last().arbitrary = true;
        } else if (const qsizetype slash = rest.indexOf(QLatin1Char('/')); slash > 0) {
            bool numeratorOk = false;
            bool denominatorOk = false;
            const double numerator = rest.left(slash).toDouble(&numeratorOk);
            const double denominator = rest.mid(slash + 1).toDouble(&denominatorOk);
            if (numeratorOk && denominatorOk && numerator >= 0 && denominator > 0) {
                addRule(&out, utility);
                out.rules.last().fraction = numerator / denominator;
            }
        }
        return out;
    }

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
        if (registry->knowsDuration(rest))
            addRule(&out, LoomUtility::TransitionDuration, rest);
        return out;
    }

    const QHash<QString, QString> &easings = easingUtilities();
    if (const auto easing = easings.constFind(name.toString());
        easing != easings.constEnd()) {
        addRule(&out, LoomUtility::TransitionEase, *easing);
        return out;
    }
    if (name.startsWith(QLatin1String("ease-"))) {
        const QString key = name.mid(qstrlen("ease-")).toString();
        if (registry->knowsEasing(key))
            addRule(&out, LoomUtility::TransitionEase, key);
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
        double width = rest.toDouble(&isNumber);
        const auto arbitraryWidth = bracketNumber(rest);
        if (arbitraryWidth) {
            width = *arbitraryWidth;
            isNumber = true;
        }
        // toDouble also accepts "nan", "inf" and negatives, none of which are a
        // border width. Reject them as unknown classes rather than writing
        // nonsense into the target's border.
        if (isNumber && std::isfinite(width) && width >= 0) {
            addRule(&out, LoomUtility::BorderWidth, QString(), width);
            out.rules.last().arbitrary = arbitraryWidth.has_value();
        } else if (!isNumber && registry->knowsColor(rest)) {
            addRule(&out, LoomUtility::BorderColor, rest);
        } else if (
            !isNumber && rest.startsWith(QLatin1Char('['))
            && rest.endsWith(QLatin1Char(']'))
            && QColor::fromString(rest.sliced(1, rest.size() - 2)).isValid()) {
            addRule(&out, LoomUtility::BorderColor, rest.sliced(1, rest.size() - 2));
            out.rules.last().arbitrary = true;
        }
        return out;
    }

    if (const auto direction = gradientDirections().constFind(name.toString());
        direction != gradientDirections().constEnd()) {
        addRule(&out, LoomUtility::GradientDirection, QString(), *direction);
        return out;
    }

    const auto colorUtility = [registry](QStringView value, LoomUtility utility) {
        Parsed parsed;
        const QString key = value.toString();
        if (registry->knowsColor(key)) {
            addRule(&parsed, utility, key);
        } else if (
            key.startsWith(QLatin1Char('[')) && key.endsWith(QLatin1Char(']'))
            && QColor::fromString(key.sliced(1, key.size() - 2)).isValid()) {
            addRule(&parsed, utility, key.sliced(1, key.size() - 2));
            parsed.rules.last().arbitrary = true;
        }
        return parsed;
    };
    for (const auto &[prefix, utility] : {
             std::pair{QLatin1StringView("from-"), LoomUtility::GradientFrom},
             std::pair{QLatin1StringView("via-"), LoomUtility::GradientVia},
             std::pair{QLatin1StringView("to-"), LoomUtility::GradientTo},
         }) {
        if (name.startsWith(prefix))
            return colorUtility(name.mid(prefix.size()), utility);
    }

    if (name == QLatin1String("ring")) {
        addRule(&out, LoomUtility::RingWidth, QString(), 2);
        return out;
    }
    if (name.startsWith(QLatin1String("ring-"))) {
        const QString rest = name.mid(qstrlen("ring-")).toString();
        bool numeric = false;
        double width = rest.toDouble(&numeric);
        if (const auto arbitrary = bracketNumber(rest)) {
            width = *arbitrary;
            numeric = true;
        }
        if (numeric && std::isfinite(width) && width >= 0) {
            addRule(&out, LoomUtility::RingWidth, QString(), width);
            out.rules.last().arbitrary = rest.startsWith(QLatin1Char('['));
        } else {
            out = colorUtility(rest, LoomUtility::RingColor);
        }
        return out;
    }

    static const QHash<QString, double> blurValues{
        {QStringLiteral("none"), 0}, {QStringLiteral("sm"), 4},
        {QStringLiteral("base"), 8}, {QStringLiteral("md"), 12},
        {QStringLiteral("lg"), 16},  {QStringLiteral("xl"), 24},
        {QStringLiteral("2xl"), 40}, {QStringLiteral("3xl"), 64},
    };
    if (name == QLatin1String("blur")) {
        addRule(&out, LoomUtility::FilterBlur, QString(), 8);
        return out;
    }
    if (name.startsWith(QLatin1String("blur-"))) {
        const QString value = name.mid(qstrlen("blur-")).toString();
        if (const auto found = blurValues.constFind(value);
            found != blurValues.constEnd())
            addRule(&out, LoomUtility::FilterBlur, QString(), *found);
        else if (
            const auto arbitrary = bracketNumber(value); arbitrary && *arbitrary >= 0) {
            addRule(&out, LoomUtility::FilterBlur, QString(), *arbitrary);
            out.rules.last().arbitrary = true;
        }
        return out;
    }
    if (name == QLatin1String("grayscale")) {
        addRule(&out, LoomUtility::FilterSaturation, QString(), 0);
        return out;
    }
    if (name == QLatin1String("grayscale-0")) {
        addRule(&out, LoomUtility::FilterSaturation, QString(), 100);
        return out;
    }
    for (const auto &[prefix, utility] : {
             std::pair{QLatin1StringView("brightness-"), LoomUtility::FilterBrightness},
             std::pair{QLatin1StringView("contrast-"), LoomUtility::FilterContrast},
             std::pair{QLatin1StringView("saturate-"), LoomUtility::FilterSaturation},
         }) {
        if (!name.startsWith(prefix))
            continue;
        const QStringView value = name.mid(prefix.size());
        bool ok = false;
        double percent = value.toDouble(&ok);
        if (const auto arbitrary = bracketNumber(value)) {
            percent = *arbitrary;
            ok = true;
        }
        if (ok && std::isfinite(percent) && percent >= 0) {
            addRule(&out, utility, QString(), percent);
            out.rules.last().arbitrary = value.startsWith(u'[');
        }
        return out;
    }

    if (name.startsWith(QLatin1String("bg-"))) {
        const QString rest = name.mid(qsizetype(qstrlen("bg-"))).toString();
        if (registry->knowsColor(rest))
            addRule(&out, LoomUtility::BgColor, rest);
        else if (
            rest.startsWith(QLatin1Char('[')) && rest.endsWith(QLatin1Char(']'))
            && QColor::fromString(rest.sliced(1, rest.size() - 2)).isValid()) {
            addRule(&out, LoomUtility::BgColor, rest.sliced(1, rest.size() - 2));
            out.rules.last().arbitrary = true;
        }
        return out;
    }

    if (name.startsWith(QLatin1String("text-"))) {
        // Size keys win over color lookup, matching Tailwind.
        const QString rest = name.mid(qsizetype(qstrlen("text-"))).toString();
        if (registry->knowsTextSize(rest))
            addRule(&out, LoomUtility::TextSize, rest);
        else if (registry->knowsColor(rest))
            addRule(&out, LoomUtility::TextColor, rest);
        else if (rest.startsWith(QLatin1Char('[')) && rest.endsWith(QLatin1Char(']'))) {
            const QString value = rest.sliced(1, rest.size() - 2);
            if (QColor::fromString(value).isValid()) {
                addRule(&out, LoomUtility::TextColor, value);
                out.rules.last().arbitrary = true;
            } else if (
                const auto arbitrary = bracketNumber(rest); arbitrary && *arbitrary > 0) {
                addRule(&out, LoomUtility::TextSize, QString(), *arbitrary);
                out.rules.last().arbitrary = true;
            }
        }
        return out;
    }

    if (name.startsWith(QLatin1String("font-"))) {
        const QString rest = name.mid(qsizetype(qstrlen("font-"))).toString();
        if (registry->knowsFontWeight(rest))
            addRule(&out, LoomUtility::FontWeight, rest);
        else if (registry->knowsFontFamily(rest))
            addRule(&out, LoomUtility::FontFamily, rest);
        else if (
            rest.startsWith(QLatin1Char('[')) && rest.endsWith(QLatin1Char(']'))
            && rest.size() > 2) {
            QString family = rest.sliced(1, rest.size() - 2);
            family.replace(QLatin1Char('_'), QLatin1Char(' '));
            addRule(&out, LoomUtility::FontFamily, family);
            out.rules.last().arbitrary = true;
        }
        return out;
    }

    if (name.startsWith(QLatin1String("leading-"))) {
        const QString rest = name.mid(qstrlen("leading-")).toString();
        if (registry->knowsTextSize(rest))
            addRule(&out, LoomUtility::LineHeight, rest);
        else if (const auto arbitrary = bracketNumber(rest)) {
            addRule(&out, LoomUtility::LineHeight, QString(), *arbitrary);
            out.rules.last().arbitrary = true;
        }
        return out;
    }

    if (name.startsWith(QLatin1String("tracking-"))) {
        const QString rest = name.mid(qsizetype(qstrlen("tracking-"))).toString();
        if (registry->knowsTracking(rest))
            addRule(&out, LoomUtility::Tracking, rest);
        else if (const auto arbitrary = bracketNumber(rest)) {
            addRule(&out, LoomUtility::Tracking, QString(), *arbitrary);
            out.rules.last().arbitrary = true;
        }
        return out;
    }

    if (name.startsWith(QLatin1String("opacity-"))) {
        const QString rest = name.mid(qsizetype(qstrlen("opacity-"))).toString();
        if (registry->knowsOpacityValue(rest))
            addRule(&out, LoomUtility::Opacity, rest);
        else if (const auto arbitrary = bracketNumber(rest)) {
            if (*arbitrary >= 0 && *arbitrary <= 1) {
                addRule(&out, LoomUtility::Opacity, QString(), *arbitrary);
                out.rules.last().arbitrary = true;
            }
        }
        return out;
    }

    if (name == QLatin1String("shadow")) {
        addRule(&out, LoomUtility::Shadow, QStringLiteral("base"));
        return out;
    }
    if (name.startsWith(QLatin1String("shadow-"))) {
        const QString rest = name.mid(qsizetype(qstrlen("shadow-"))).toString();
        if (registry->knowsShadow(rest))
            addRule(&out, LoomUtility::Shadow, rest);
        return out;
    }

    if (name.startsWith(QLatin1String("gap-"))) {
        const QString rest = name.mid(qsizetype(qstrlen("gap-"))).toString();
        if (registry->knowsSpace(rest))
            addRule(&out, LoomUtility::Gap, rest);
        else if (
            const auto arbitrary = bracketNumber(rest); arbitrary && *arbitrary >= 0) {
            addRule(&out, LoomUtility::Gap, QString(), *arbitrary);
            out.rules.last().arbitrary = true;
        }
        return out;
    }

    if (name.startsWith(QLatin1String("w-"))) {
        const QString rest = name.mid(qsizetype(qstrlen("w-"))).toString();
        if (registry->knowsSpace(rest))
            addRule(&out, LoomUtility::Width, rest);
        else if (const auto arbitrary = bracketNumber(rest)) {
            addRule(&out, LoomUtility::Width, QString(), *arbitrary);
            out.rules.last().arbitrary = true;
        } else if (const qsizetype slash = rest.indexOf(QLatin1Char('/')); slash > 0) {
            bool leftOk = false;
            bool rightOk = false;
            const double left = rest.left(slash).toDouble(&leftOk);
            const double right = rest.mid(slash + 1).toDouble(&rightOk);
            if (leftOk && rightOk && left >= 0 && right > 0) {
                addRule(&out, LoomUtility::Width);
                out.rules.last().fraction = left / right;
            }
        }
        return out;
    }
    if (name.startsWith(QLatin1String("h-"))) {
        const QString rest = name.mid(qsizetype(qstrlen("h-"))).toString();
        if (registry->knowsSpace(rest))
            addRule(&out, LoomUtility::Height, rest);
        else if (const auto arbitrary = bracketNumber(rest)) {
            addRule(&out, LoomUtility::Height, QString(), *arbitrary);
            out.rules.last().arbitrary = true;
        } else if (const qsizetype slash = rest.indexOf(QLatin1Char('/')); slash > 0) {
            bool leftOk = false;
            bool rightOk = false;
            const double left = rest.left(slash).toDouble(&leftOk);
            const double right = rest.mid(slash + 1).toDouble(&rightOk);
            if (leftOk && rightOk && left >= 0 && right > 0) {
                addRule(&out, LoomUtility::Height);
                out.rules.last().fraction = left / right;
            }
        }
        return out;
    }
    if (name.startsWith(QLatin1String("size-"))) {
        const QString rest = name.mid(qsizetype(qstrlen("size-"))).toString();
        if (registry->knowsSpace(rest)) {
            addRule(&out, LoomUtility::Width, rest);
            addRule(&out, LoomUtility::Height, rest);
        } else if (
            const auto arbitrary = bracketNumber(rest); arbitrary && *arbitrary >= 0) {
            addRule(&out, LoomUtility::Width, QString(), *arbitrary);
            out.rules.last().arbitrary = true;
            addRule(&out, LoomUtility::Height, QString(), *arbitrary);
            out.rules.last().arbitrary = true;
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
        if (registry->knowsSpace(rest)) {
            addRule(&out, family.utility, rest);
        } else if (
            const auto arbitrary = bracketNumber(rest); arbitrary && *arbitrary >= 0) {
            addRule(&out, family.utility, QString(), *arbitrary);
            out.rules.last().arbitrary = true;
        }
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
    case LoomUtility::FontFamily:
        return "font-{family}";
    case LoomUtility::Italic:
        return "italic";
    case LoomUtility::Underline:
        return "underline";
    case LoomUtility::LineThrough:
        return "line-through";
    case LoomUtility::Tracking:
        return "tracking-*";
    case LoomUtility::TextAlignment:
        return "text-{alignment}";
    case LoomUtility::TextElide:
        return "text-ellipsis/text-clip";
    case LoomUtility::TextMaximumLines:
        return "line-clamp-*";
    case LoomUtility::TextCapitalization:
        return "uppercase/lowercase/capitalize";
    case LoomUtility::TextWrapMode:
        return "whitespace-*";
    case LoomUtility::LineHeight:
        return "leading-*";
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
    case LoomUtility::Clip:
        return "overflow-*";
    case LoomUtility::ZOrder:
        return "z-*";
    case LoomUtility::Rotation:
        return "rotate-*";
    case LoomUtility::Scale:
        return "scale-*";
    case LoomUtility::TransformOrigin:
        return "origin-*";
    case LoomUtility::CursorShape:
        return "cursor-*";
    case LoomUtility::TranslateX:
        return "translate-x-*";
    case LoomUtility::TranslateY:
        return "translate-y-*";
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
    case LoomUtility::RingWidth:
        return "ring-{width}";
    case LoomUtility::RingColor:
        return "ring-{color}";
    case LoomUtility::GradientDirection:
        return "bg-linear-to-*";
    case LoomUtility::GradientFrom:
        return "from-*";
    case LoomUtility::GradientVia:
        return "via-*";
    case LoomUtility::GradientTo:
        return "to-*";
    case LoomUtility::FilterBlur:
        return "blur-*";
    case LoomUtility::FilterBrightness:
        return "brightness-*";
    case LoomUtility::FilterContrast:
        return "contrast-*";
    case LoomUtility::FilterSaturation:
        return "saturate-*/grayscale";
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
    case LoomUtility::RingColor:
    case LoomUtility::GradientFrom:
    case LoomUtility::GradientVia:
    case LoomUtility::GradientTo:
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
// with alpha 9 and was rejected. Trying the base parse first also lets
// fractional sizes such as `w-1/2` reach their own parser.
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

QStringList expandRecipes(const QString &style, QStringList *invalid)
{
    const auto splitClasses = [](const QString &value) {
        static const QRegularExpression whitespace(QStringLiteral("\\s+"));
        return value.split(whitespace, Qt::SkipEmptyParts);
    };
    QStringList expanded;
    QStringList stack;
    std::function<void(const QString &, const QString &, int)> expandOne;
    expandOne = [&](const QString &klass, const QString &inherited, int depth) {
        if (expanded.size() >= 4096 || depth > 32) {
            invalid->append(inherited + klass);
            return;
        }
        const QStringList segments = klass.split(QLatin1Char(':'));
        const QString utility = segments.constLast();
        QString prefix = inherited;
        if (segments.size() > 1)
            prefix += segments.first(segments.size() - 1).join(QLatin1Char(':'))
                + QLatin1Char(':');
        if (!utility.startsWith(QLatin1Char('@'))) {
            expanded.append(prefix + utility);
            return;
        }
        const QString name = utility.mid(1);
        auto *registry = LoomTokenRegistry::instance();
        if (!registry->hasStyleRecipe(name) || stack.contains(name)) {
            invalid->append(prefix + utility);
            return;
        }
        stack.append(name);
        const auto classes = splitClasses(registry->styleRecipe(name));
        for (const QString &nested : classes)
            expandOne(nested, prefix, depth + 1);
        stack.removeLast();
    };
    for (const QString &klass : splitClasses(style))
        expandOne(klass, {}, 0);
    return expanded;
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
    QStringList invalidRecipes;
    const QStringList classes = expandRecipes(style, &invalidRecipes);
    for (const QString &invalid : invalidRecipes)
        qCWarning(lcLoomStyle).noquote()
            << "Lo.style: unknown or cyclic style recipe" << invalid << "in" << style;
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
            rule.stateNotMask = parse.variant.stateNotMask;
            rule.minWidth = parse.variant.minWidth;
            rule.maxWidth = parse.variant.maxWidth;
            rule.containerMinWidth = parse.variant.containerMinWidth;
            rule.containerMaxWidth = parse.variant.containerMaxWidth;
            rule.containerName = parse.variant.containerName;
            rule.groupStateMask = parse.variant.groupStateMask;
            rule.groupStateNotMask = parse.variant.groupStateNotMask;
            rule.customMask = parse.variant.customMask;
            rule.customNotMask = parse.variant.customNotMask;
            rule.groupCustomMask = parse.variant.groupCustomMask;
            rule.groupCustomNotMask = parse.variant.groupCustomNotMask;
            rule.groupName = parse.variant.groupName;
            rule.themeName = parse.variant.themeName;
            // Custom states rank exactly like built-in ones: both are transient
            // conditions that should override the static appearance, and there
            // is no reading under which `invalid:` is inherently weaker or
            // stronger than `hover:`. At equal depth the later class wins,
            // which is the existing rule, and `hover:invalid:` beats both.
            rule.specificity = loomSpecificity(
                rule.minWidth, rule.maxWidth, rule.containerMinWidth,
                rule.containerMaxWidth,
                parse.variant.stateMask | parse.variant.stateNotMask,
                !rule.containerName.isEmpty(),
                quint8(
                    std::popcount(
                        parse.variant.groupStateMask | parse.variant.groupStateNotMask))
                    + quint8(std::popcount(
                        parse.variant.customMask | parse.variant.customNotMask))
                    + quint8(std::popcount(
                        parse.variant.groupCustomMask | parse.variant.groupCustomNotMask))
                    + quint8(!parse.variant.themeName.isEmpty()));
            compiled->usedStates |= rule.stateMask | rule.stateNotMask;
            compiled->usedCustomStates |= rule.customMask | rule.customNotMask;
            if (rule.minWidth > 0 || rule.maxWidth != std::numeric_limits<int>::max())
                compiled->usesBreakpoints = true;
            if (rule.containerMinWidth > 0
                || rule.containerMaxWidth != std::numeric_limits<int>::max())
                compiled->usesContainers = true;
            if (rule.groupStateMask || rule.groupStateNotMask || rule.groupCustomMask
                || rule.groupCustomNotMask)
                compiled->usesGroups = true;
            if (rule.utility == LoomUtility::WidthFull
                || rule.utility == LoomUtility::HeightFull || rule.fraction > 0)
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
            if (rule.utility == LoomUtility::CursorShape)
                compiled->usesCursor = true;
            if (rule.utility == LoomUtility::TranslateX
                || rule.utility == LoomUtility::TranslateY)
                compiled->usesTranslate = true;
            if (rule.utility >= LoomUtility::RingWidth
                && rule.utility <= LoomUtility::FilterSaturation)
                compiled->usesEffects = true;
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
    const QStringList classes = expandRecipes(style, &unknown);
    for (const QString &klass : classes) {
        if (!parseClass(klass).ok)
            unknown.append(klass);
    }
    return unknown;
}

bool isBuiltinVariant(const QString &name)
{
    // Used by the config loader to reject a declared state that would be
    // shadowed by the built-in table parseVariant() consults first.
    auto *registry = LoomTokenRegistry::instance();
    return stateVariants().contains(name) || registry->hasBreakpoint(name)
        || registry->hasContainer(name) || registry->themeNames().contains(name);
}

QStringList variantNames()
{
    QStringList names = LoomTokenRegistry::instance()->breakpointKeys();
    for (const QString &breakpoint : LoomTokenRegistry::instance()->breakpointKeys())
        names.append(QStringLiteral("max-") + breakpoint);
    names.append(stateVariants().keys());
    for (const QString &state : stateVariants().keys()) {
        names.append(QStringLiteral("not-") + state);
        names.append(QStringLiteral("group-") + state);
    }
    // Declared application states advertise the same three forms. Everything
    // downstream -- `loom style --catalogue`, LSP completion, the typo
    // suggester -- reads this list, so they need no knowledge of their own, and
    // tst_catalogue's round-trip check covers the new names automatically.
    for (const QString &state : LoomTokenRegistry::instance()->customStateNames()) {
        names.append(state);
        names.append(QStringLiteral("not-") + state);
        names.append(QStringLiteral("group-") + state);
    }
    for (const QString &container : LoomTokenRegistry::instance()->containerKeys()) {
        names.append(QLatin1Char('@') + container);
        names.append(QStringLiteral("@max-") + container);
    }
    for (const QString &theme : LoomTokenRegistry::instance()->themeNames())
        names.append(QStringLiteral("theme-") + theme);
    names.sort();
    return names;
}

QStringList valuelessClasses()
{
    QStringList names = exactUtilities().keys();
    names.append(transitionModeUtilities().keys());
    names.append(easingUtilities().keys());
    names.append(alignmentUtilities().keys());
    names.append(cursorUtilities().keys());
    names.append(gradientDirections().keys());
    // The named ratios only. `aspect-16/9` takes a value that is not a token,
    // so it is advertised through numericPrefixes instead.
    names.append(aspectUtilities().keys());
    // Both take an optional value, so the bare forms are valid classes too:
    // `rounded` is 4px and `border` is 1px.
    names.append(QStringLiteral("rounded"));
    names.append(QStringLiteral("border"));
    names.append(QStringLiteral("shadow"));
    names.append(
        {QStringLiteral("invisible"),
         QStringLiteral("truncate"),
         QStringLiteral("text-left"),
         QStringLiteral("text-center"),
         QStringLiteral("text-right"),
         QStringLiteral("text-justify"),
         QStringLiteral("text-ellipsis"),
         QStringLiteral("text-clip"),
         QStringLiteral("uppercase"),
         QStringLiteral("lowercase"),
         QStringLiteral("capitalize"),
         QStringLiteral("normal-case"),
         QStringLiteral("whitespace-normal"),
         QStringLiteral("whitespace-nowrap"),
         QStringLiteral("wrap-anywhere"),
         QStringLiteral("line-clamp-none"),
         QStringLiteral("origin-top-left"),
         QStringLiteral("origin-top"),
         QStringLiteral("origin-top-right"),
         QStringLiteral("origin-left"),
         QStringLiteral("origin-center"),
         QStringLiteral("origin-right"),
         QStringLiteral("origin-bottom-left"),
         QStringLiteral("origin-bottom"),
         QStringLiteral("origin-bottom-right"),
         QStringLiteral("ring"),
         QStringLiteral("blur"),
         QStringLiteral("grayscale"),
         QStringLiteral("grayscale-0")});
    for (int lines = 1; lines <= 6; ++lines)
        names.append(QStringLiteral("line-clamp-%1").arg(lines));
    names.sort();
    return names;
}

} // namespace LoomStyleCompiler
