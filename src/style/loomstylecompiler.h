#pragma once

#include <QString>
#include <QVector>
#include <bit>
#include <memory>

// Compiled form of a `Lo.style` utility string. Rules store token *identities*
// (registry keys), never resolved values: resolution happens at apply time, so
// a theme switch re-applies without recompiling.

enum class LoomUtility : quint8 {
    BgColor,
    TextColor,
    TextSize,
    FontWeight,
    Italic,
    Underline,
    LineThrough,
    Tracking,
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
    Shadow,
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

enum LoomState : quint8 {
    LoomHoverState = 1,
    LoomPressedState = 2,
    LoomFocusState = 4,
    LoomDisabledState = 8,
    LoomDarkState = 16,
};

// Which rule wins when two of them set the same property. Breakpoints and
// states are separate axes, and states are the stronger one: a state variant is
// a transient condition that should override the static appearance at any
// width, so `hover:` beats `md:` while `md:hover:` beats both. Ranking the two
// on a single count of variant prefixes made the later-written class win every
// tie, which silently killed every `hover:` rule as soon as a `md:` rule
// existed for the same property.
constexpr quint8 loomSpecificity(quint8 minBreakpoint, quint8 stateMask)
{
    return quint8((std::popcount(stateMask) << 3) | minBreakpoint);
}

struct LoomStyleRule {
    LoomUtility utility;
    quint8 minBreakpoint = 0;  // 0 = base, 1..4 = sm..xl
    quint8 stateMask = 0;      // LoomState bits that must all be active
    quint8 specificity = 0;    // loomSpecificity(minBreakpoint, stateMask)
    QString key;               // registry key for token-valued utilities
    double literal = 0;        // numeric literal (border width)
    quint8 alphaPercent = 100; // `bg-surface/70` colour-opacity modifier
    bool flag = false;         // bool-valued utilities (visible, italic, ...)
};

class LoomCompiledStyle {
public:
    QVector<LoomStyleRule> rules;
    quint8 usedStates = 0; // OR of every rule's stateMask
    bool usesBreakpoints = false;
    bool usesParentSize = false; // w-full / h-full
    bool usesMargins = false;    // m-* re-routes between anchors and Layout
    bool usesLayout = false;     // fill / center / pin-* / self-* / min-w-* ...
    bool usesAspect = false;     // aspect-* derives height from the item's width
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

// The variant prefixes the parser accepts (`sm` … `xl`, `hover` … `dark`),
// sorted. Enumerated from the parser's own tables.
QStringList variantNames();

// Every utility class that takes no value (`italic`, `w-full`, `transition-*`,
// …), sorted. Value-taking families are enumerated by the catalogue, which
// pairs each prefix with the relevant token scale.
QStringList valuelessClasses();

} // namespace LoomStyleCompiler
