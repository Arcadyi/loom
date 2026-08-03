#pragma once

#include <QString>
#include <QVector>
#include <algorithm>
#include <bit>
#include <limits>
#include <memory>

// Compiled form of a `Lo.style` utility string. Rules store token *identities*
// (registry keys), never resolved values: resolution happens at apply time, so
// a theme switch re-applies without recompiling.

enum class LoomUtility : quint8 {
    BgColor,
    TextColor,
    TextSize,
    FontWeight,
    FontFamily,
    Italic,
    Underline,
    LineThrough,
    Tracking,
    TextAlignment,
    TextElide,
    TextMaximumLines,
    TextCapitalization,
    TextWrapMode,
    LineHeight,
    PaddingTop,
    PaddingRight,
    PaddingBottom,
    PaddingLeft,
    MarginTop,
    MarginRight,
    MarginBottom,
    MarginLeft,
    Gap,
    Width,
    Height,
    WidthFull,
    HeightFull,
    Radius,
    RadiusTopLeft,
    RadiusTopRight,
    RadiusBottomRight,
    RadiusBottomLeft,
    BorderWidth,
    BorderColor,
    Opacity,
    Visible,
    Clip,
    ZOrder,
    Rotation,
    Scale,
    TransformOrigin,
    // Layout. These resolve to anchors outside a QtQuick.Layouts layout and to
    // the Layout.* attached properties inside one, decided per apply -- see
    // LoomStyleAttached::layoutPaths(). Anchoring an item a layout manages is
    // undefined behaviour that Qt warns about, so the routing is a correctness
    // requirement rather than a convenience.
    AnchorFill,
    AnchorFillX,
    AnchorFillY,
    AnchorCenter,
    AnchorCenterX,
    AnchorCenterY,
    AnchorPinTop,
    AnchorPinRight,
    AnchorPinBottom,
    AnchorPinLeft,
    LayoutAlignment, // literal carries a Qt::Alignment
    LayoutMinWidth,
    LayoutMaxWidth,
    LayoutMinHeight,
    LayoutMaxHeight,
    LayoutColumnSpan, // literal carries the span
    LayoutRowSpan,
    AspectRatio, // literal carries width / height
    // Managed, non-property visuals. Translate is appended to Item.transform,
    // cursor uses QQuickItem's native cursor, and the effect families are
    // synchronized after the ordinary property-write pass.
    CursorShape,
    TranslateX,
    TranslateY,
    Shadow,
    RingWidth,
    RingColor,
    GradientDirection,
    GradientFrom,
    GradientVia,
    GradientTo,
    FilterBlur,
    FilterBrightness,
    FilterContrast,
    FilterSaturation,
    TransitionMode,     // literal carries a LoomTransitionMode
    TransitionDuration, // key is a duration token
    TransitionEase,     // key is an easing token
};

// What property changes animate. Default is Tailwind's `transition` shorthand
// scope reduced to what Loom manages: colors and opacity.
enum class LoomTransitionMode : quint8 {
    None,
    Default,
    Colors,
    Opacity,
    All,
};

enum LoomState : quint32 {
    LoomHoverState = 1,
    LoomPressedState = 2,
    LoomFocusState = 4,
    LoomDisabledState = 8,
    LoomDarkState = 16,
    LoomCheckedState = 1u << 5,
    LoomDownState = 1u << 6,
    LoomHighlightedState = 1u << 7,
    LoomSelectedState = 1u << 8,
    LoomEditableState = 1u << 9,
    LoomReadOnlyState = 1u << 10,
    LoomActiveState = 1u << 11,
    LoomFocusWithinState = 1u << 12,
    LoomFocusVisibleState = 1u << 13,
    LoomRtlState = 1u << 14,
    LoomLtrState = 1u << 15,
    LoomPortraitState = 1u << 16,
    LoomLandscapeState = 1u << 17,
    LoomWindowActiveState = 1u << 18,
    LoomHighContrastState = 1u << 19,
    LoomMotionReduceState = 1u << 20,
    LoomFirstState = 1u << 21,
    LoomLastState = 1u << 22,
    LoomOnlyState = 1u << 23,
    LoomOddState = 1u << 24,
    LoomEvenState = 1u << 25,
    // Built-in rather than application-declared, because Loom.Controls.Field
    // ships using it: a component cannot require the application to have
    // configured a state before it renders correctly. Sourced from a `bool
    // invalid` property on the target, the same duck-typing `checked` and
    // `readOnly` use -- Qt has no such property of its own, but every
    // validating input grows one.
    LoomInvalidState = 1u << 26,
};

// Which rule wins when two of them set the same property. Responsive and state
// conditions are separate axes, and states are the stronger one: a state variant is
// a transient condition that should override the static appearance at any
// width, so `hover:` beats `md:` while `md:hover:` beats both. Ranking the two
// on a single count of variant prefixes made the later-written class win every
// tie, which silently killed every `hover:` rule as soon as a `md:` rule
// existed for the same property.
struct LoomStyleRule {
    LoomUtility utility;
    quint8 minBreakpoint = 0;  // conventional named-breakpoint order, for metadata
    quint32 stateMask = 0;     // LoomState bits that must all be active
    quint32 stateNotMask = 0;  // LoomState bits that must all be inactive
    quint64 specificity = 0;   // state depth, then exact responsive constraints
    QString key;               // registry key for token-valued utilities
    double literal = 0;        // numeric literal (border width)
    quint8 alphaPercent = 100; // `bg-surface/70` colour-opacity modifier
    bool flag = false;         // bool-valued utilities (visible, italic, ...)
    bool arbitrary = false;    // literal/key came from a typed [...] value
    bool negative = false;     // numeric token/literal is negated
    double fraction = 0;       // parent-relative width/height; 0 = not fractional
    int minWidth = 0;          // viewport constraint in px
    int maxWidth = std::numeric_limits<int>::max();
    int containerMinWidth = 0;
    int containerMaxWidth = std::numeric_limits<int>::max();
    QString containerName;
    quint32 groupStateMask = 0;
    quint32 groupStateNotMask = 0;
    // Application-declared states, in a channel of their own rather than in
    // spare LoomState bits. LoomState means "Loom knows how to observe this
    // from Qt"; these mean "the application tells us". Keeping them apart also
    // leaves the six remaining LoomState bits for future built-ins, and avoids
    // widening stateMask -- a change that would reach every state site across
    // the apply engine to buy nothing this does not.
    quint32 customMask = 0;
    quint32 customNotMask = 0;
    quint32 groupCustomMask = 0;
    quint32 groupCustomNotMask = 0;
    QString groupName;
    QString themeName;
};

// State/group/theme conditions are the strongest axis. Responsive depth is
// next, followed by exact viewport and container bounds. Using the bounds (not
// a four-tier enum) makes config-defined, max-width, arbitrary, and container
// variants correctly outrank an unqualified rule and order deterministically.
constexpr quint64 loomSpecificity(
    int minWidth, int maxWidth, int containerMinWidth, int containerMaxWidth,
    quint32 stateMask, bool namedContainer = false, quint8 additionalStateDepth = 0)
{
    constexpr quint64 boundLimit = 8191;
    const auto lowerRank = [=](int value) {
        return quint64(std::clamp(value, 0, int(boundLimit)));
    };
    const auto upperRank = [&](int value) {
        return value == std::numeric_limits<int>::max() ? quint64(0)
                                                        : boundLimit - lowerRank(value);
    };
    const quint64 conditionCount = quint64(minWidth > 0)
        + quint64(maxWidth != std::numeric_limits<int>::max())
        + quint64(containerMinWidth > 0)
        + quint64(containerMaxWidth != std::numeric_limits<int>::max());
    // Bits 58-63 hold the depth: six bits, so 63 is the largest value that
    // survives the shift. The clamp is not theoretical. Before custom states
    // the worst case was around 53 and the field could not overflow; a rule
    // combining several declared states with a group and a theme reaches past
    // 63, and without this the shift would silently truncate and reorder every
    // rule in the style -- a miscompare no one would think to look for here.
    constexpr quint64 stateDepthLimit = 63;
    const quint64 stateDepth = std::min<quint64>(
        quint64(std::popcount(stateMask)) + additionalStateDepth, stateDepthLimit);
    return (stateDepth << 58) | (conditionCount << 54) | (lowerRank(minWidth) << 41)
        | (upperRank(maxWidth) << 28) | (lowerRank(containerMinWidth) << 15)
        | (upperRank(containerMaxWidth) << 2) | (quint64(namedContainer) << 1);
}

// Compatibility helper used by code/tests that reason in conventional named
// tiers. Runtime compilation uses the exact-bounds overload above.
constexpr quint64 loomSpecificity(quint8 minBreakpoint, quint32 stateMask)
{
    constexpr int conventional[] = {0, 640, 768, 1024, 1280, 1536, 1792, 2048};
    const int index = std::min<int>(minBreakpoint, 7);
    return loomSpecificity(
        conventional[index], std::numeric_limits<int>::max(), 0,
        std::numeric_limits<int>::max(), stateMask);
}

class LoomCompiledStyle {
public:
    QVector<LoomStyleRule> rules;
    quint32 usedStates = 0;       // OR of every rule's positive and negative states
    quint32 usedCustomStates = 0; // the same, for application-declared states
    bool usesBreakpoints = false;
    bool usesParentSize = false; // w-full / h-full
    bool usesMargins = false;    // m-* re-routes between anchors and Layout
    bool usesLayout = false;     // fill / center / pin-* / self-* / min-w-* ...
    bool usesAspect = false;     // aspect-* derives height from the item's width
    bool usesContainers = false;
    bool usesGroups = false;
    bool usesCursor = false;
    bool usesTranslate = false;
    bool usesEffects = false;
};

// The utility family a rule came from, spelled the way a user writes it, for
// diagnostics. A switch rather than Q_ENUM: -Wswitch then makes a missing case
// a build error, where Q_ENUM would quietly report an empty name -- which is
// the failure this exists to fix.
const char *loomUtilityName(LoomUtility utility);

namespace LoomStyleCompiler {

// Parse a utility string into a shared, immutable compiled style. Results are
// cached process-wide by exact string; unknown classes warn once per unique
// string (category loom.style) and are skipped.
std::shared_ptr<const LoomCompiledStyle> compile(const QString &style);

// Drop the cache. Called when a config load changes the token vocabulary
// (a cached compile may have rejected `bg-brand-500` before the config defined
// it) and by tests.
void clearCache();

// The classes in `style` that no utility recognises, in the order they appear.
// Same parse as compile(), but for tooling: it neither warns nor caches, so a
// checker can report per-occurrence instead of once per unique string.
QStringList unknownClasses(const QString &style);

// The enumerable variant prefixes the parser accepts, sorted. Includes live
// breakpoint/container names plus state, theme, structural, group, and
// application-declared forms; typed arbitrary viewport/container queries are
// recognized but not enumerable.
QStringList variantNames();

// True when the name already means something to parseVariant() before any
// application state is consulted. The config loader rejects a declared state
// that would collide, because parseVariant() asks the built-in table first and
// the declaration would otherwise be silently unreachable.
bool isBuiltinVariant(const QString &name);

// Every utility class that takes no value (`italic`, `w-full`, `transition-*`,
// …), sorted. Value-taking families are enumerated by the catalogue, which
// pairs each prefix with the relevant token scale.
QStringList valuelessClasses();

} // namespace LoomStyleCompiler
