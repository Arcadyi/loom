#pragma once

#include <QString>

// Canonical ordering for a class string.
//
// The reason this is not a plain sort: `compile()` gives later classes the win
// at equal specificity, so `p-4 p-6` and `p-6 p-4` compile *differently*, and
// so do `p-4 px-6` and `px-6 p-4`. Reordering a class string can silently
// change what it paints.
//
// So the sort is best-effort and then checked. Classes are ranked by
// specificity and then by the utility they write, stably; the result is
// compiled and compared against the original rule for rule, and the original
// string is returned unchanged when they differ. A string this cannot order
// safely is left exactly as it was rather than approximated.
namespace loom::styleorder {

// The classes of `style`, reordered. Returns `style` itself when reordering
// would change what it compiles to, and when there is nothing to do.
QString canonicalOrder(const QString &style);

// Whether `style` is already in canonical order. Cheaper to read at a call
// site than comparing strings.
inline bool isOrdered(const QString &style)
{
    return canonicalOrder(style) == style;
}

} // namespace loom::styleorder
