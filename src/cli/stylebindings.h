#pragma once

#include <QLatin1StringView>
#include <QStringView>

// The property names whose value is a Loom class string.
//
// `Lo.style` is the attached property every item has. The rest are the
// part-style convention from Loom.Controls: a control that owns a sub-delegate
// the style engine cannot reach -- a CheckBox's indicator, a Slider's groove,
// a Popup's background -- forwards a `<part>Style` property onto that part's
// own `Lo.style`.
//
// The engine needs none of this; forwarding is ordinary QML. The *tooling*
// does. LoomStyleAttached is reached through an attached property whose name
// the scanner can match on, and a forwarded property is not, so without this
// list every class string in the library's part-styling surface would be
// invisible to completion, to `loom lint`, and to the diagnostics that exist
// precisely because a class string is not checked by the compiler.
//
// KNOWN TRADEOFF: this is a fixed list matched by name alone, so an unrelated
// `property string labelStyle` holding something that is not a class string is
// diagnosed as if it were. A `*Style` suffix rule would be worse -- it would
// claim far more names on far less evidence. Adding a name here is a
// deliberate act, and tests/controls_partstyle_test.cmake asserts that every
// part-style property the shipped controls declare appears below.
namespace loom::stylebindings {

inline constexpr const char *kNames[] = {
    "Lo.style",
    "contentStyle",
    "grooveStyle",
    "handleStyle",
    "headerStyle",
    "indicatorStyle",
    "labelStyle",
    "messageStyle",
    "popupStyle",
    "separatorStyle",
    "trackStyle",
};

inline bool isStyleBinding(QStringView name)
{
    for (const char *candidate : kNames) {
        if (name == QLatin1StringView(candidate))
            return true;
    }
    return false;
}

} // namespace loom::stylebindings
