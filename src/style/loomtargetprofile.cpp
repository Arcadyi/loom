#include "loomtargetprofile.h"

#include <QMetaObject>
#include <QMutex>
#include <memory>
#include <unordered_map>

bool loomInheritsByName(const QMetaObject *metaObject, const char *className)
{
    for (const QMetaObject *mo = metaObject; mo; mo = mo->superClass()) {
        if (qstrcmp(mo->className(), className) == 0)
            return true;
    }
    return false;
}

namespace {

bool hasProperty(const QMetaObject *metaObject, const char *name)
{
    return metaObject->indexOfProperty(name) >= 0;
}

} // namespace

const LoomTargetProfile *LoomTargetProfile::forType(const QMetaObject *metaObject)
{
    static QMutex mutex;
    // Owning pointers so the exit-time destructor releases the cache and the
    // leak-sanitizer job stays meaningful without suppressions. unordered_map
    // because QHash requires copyable values.
    static std::unordered_map<const QMetaObject *, std::unique_ptr<LoomTargetProfile>>
        cache;

    QMutexLocker locker(&mutex);
    if (const auto cached = cache.find(metaObject); cached != cache.end())
        return cached->second.get();

    auto owned = std::unique_ptr<LoomTargetProfile>(new LoomTargetProfile);
    LoomTargetProfile *profile = owned.get();
    auto set = [profile](LoomUtility utility, const char *path) {
        profile->m_paths[int(utility)] = QLatin1String(path);
    };

    const bool isRectangle = loomInheritsByName(metaObject, "QQuickRectangle");
    const bool isTextLike = loomInheritsByName(metaObject, "QQuickText")
        || loomInheritsByName(metaObject, "QQuickTextInput")
        || loomInheritsByName(metaObject, "QQuickTextEdit");
    const bool hasFont = hasProperty(metaObject, "font");

    if (isRectangle) {
        set(LoomUtility::BgColor, "color");
        set(LoomUtility::Radius, "radius");
        set(LoomUtility::RadiusTopLeft, "topLeftRadius");
        set(LoomUtility::RadiusTopRight, "topRightRadius");
        set(LoomUtility::RadiusBottomRight, "bottomRightRadius");
        set(LoomUtility::RadiusBottomLeft, "bottomLeftRadius");
        set(LoomUtility::BorderWidth, "border.width");
        set(LoomUtility::BorderColor, "border.color");
    }
    if (isTextLike)
        set(LoomUtility::TextColor, "color");
    if (hasFont) {
        set(LoomUtility::TextSize, "font.pixelSize");
        set(LoomUtility::FontWeight, "font.weight");
        set(LoomUtility::Italic, "font.italic");
        set(LoomUtility::Underline, "font.underline");
        set(LoomUtility::LineThrough, "font.strikeout");
        set(LoomUtility::Tracking, "font.letterSpacing");
    }
    profile->m_lineHeight = hasProperty(metaObject, "lineHeight")
        && hasProperty(metaObject, "lineHeightMode");

    // Duck-typed: any type declaring the conventional property names opts in
    // (all Quick Controls, Row/Column/Grid/Flow and the Layouts for spacing,
    // and user components that declare e.g. `property real topPadding`).
    if (hasProperty(metaObject, "topPadding"))
        set(LoomUtility::PaddingTop, "topPadding");
    if (hasProperty(metaObject, "rightPadding"))
        set(LoomUtility::PaddingRight, "rightPadding");
    if (hasProperty(metaObject, "bottomPadding"))
        set(LoomUtility::PaddingBottom, "bottomPadding");
    if (hasProperty(metaObject, "leftPadding"))
        set(LoomUtility::PaddingLeft, "leftPadding");
    if (hasProperty(metaObject, "spacing"))
        set(LoomUtility::Gap, "spacing");

    // Anchor margins exist on every Item; they only take effect when the item
    // is anchored, which is documented rather than detected.
    set(LoomUtility::MarginTop, "anchors.topMargin");
    set(LoomUtility::MarginRight, "anchors.rightMargin");
    set(LoomUtility::MarginBottom, "anchors.bottomMargin");
    set(LoomUtility::MarginLeft, "anchors.leftMargin");

    // Layout utilities. Registered unconditionally: every Item has anchors, and
    // whether the Layout.* form applies depends on the item's *parent*, which is
    // per-instance and so cannot live in this per-type cache. These entries are
    // only the "the type supports this at all" gate -- LoomStyleAttached
    // ::layoutPaths() decides where the write actually lands, and reports the
    // context mismatches this table cannot see.
    set(LoomUtility::AnchorFill, "anchors.fill");
    set(LoomUtility::AnchorFillX, "anchors.left");
    set(LoomUtility::AnchorFillY, "anchors.top");
    set(LoomUtility::AnchorCenter, "anchors.centerIn");
    set(LoomUtility::AnchorCenterX, "anchors.horizontalCenter");
    set(LoomUtility::AnchorCenterY, "anchors.verticalCenter");
    set(LoomUtility::AnchorPinTop, "anchors.top");
    set(LoomUtility::AnchorPinRight, "anchors.right");
    set(LoomUtility::AnchorPinBottom, "anchors.bottom");
    set(LoomUtility::AnchorPinLeft, "anchors.left");
    set(LoomUtility::LayoutAlignment, "Layout.alignment");
    set(LoomUtility::LayoutMinWidth, "Layout.minimumWidth");
    set(LoomUtility::LayoutMaxWidth, "Layout.maximumWidth");
    set(LoomUtility::LayoutMinHeight, "Layout.minimumHeight");
    set(LoomUtility::LayoutMaxHeight, "Layout.maximumHeight");
    set(LoomUtility::LayoutColumnSpan, "Layout.columnSpan");
    set(LoomUtility::LayoutRowSpan, "Layout.rowSpan");
    set(LoomUtility::AspectRatio, "height");

    set(LoomUtility::Width, "width");
    set(LoomUtility::Height, "height");
    set(LoomUtility::WidthFull, "width");
    set(LoomUtility::HeightFull, "height");
    set(LoomUtility::Opacity, "opacity");
    set(LoomUtility::Visible, "visible");

    cache.emplace(metaObject, std::move(owned));
    return profile;
}

QString LoomTargetProfile::propertyPath(LoomUtility utility) const
{
    const int index = int(utility);
    if (index >= UtilityCount)
        return QString();
    return m_paths[index];
}

bool LoomTargetProfile::supportsLineHeight() const
{
    return m_lineHeight;
}
