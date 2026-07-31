#pragma once

#include <QString>
#include <QVector>
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

struct LoomStyleRule {
    LoomUtility utility;
    quint8 minBreakpoint = 0;  // 0 = base, 1..4 = sm..xl
    quint8 stateMask = 0;      // LoomState bits that must all be active
    quint8 variantCount = 0;   // specificity: number of variant prefixes
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
};

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
