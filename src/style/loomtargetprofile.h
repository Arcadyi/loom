#pragma once

#include <QObject>
#include <QString>

#include "loomstylecompiler.h"

// Meta-object class-name chain check, used instead of qobject_cast because
// the Qt Quick item classes only exist in private headers while their names
// are stable API in qmltypes.
bool loomInheritsByName(const QMetaObject *metaObject, const char *className);

// How each utility lands on one concrete item type: the property path written
// through QQmlProperty, or empty when the type does not support the utility
// (bg-* off a Rectangle, p-* on types without padding properties). Built once
// per QMetaObject and cached process-wide; lookups after that are an array
// index.
class LoomTargetProfile {
public:
    static const LoomTargetProfile *forType(const QMetaObject *metaObject);

    QString propertyPath(LoomUtility utility) const;
    bool supportsLineHeight() const;

private:
    LoomTargetProfile() = default;

    // Only utilities that resolve to a property path need a slot. The three
    // transition utilities are consumed before the profile is ever consulted,
    // so they sit past the end of the array on purpose -- propertyPath() range
    // checks, and this assert fires if a *new* utility is appended after
    // Shadow, which would otherwise read out of bounds.
    static constexpr int UtilityCount = int(LoomUtility::Shadow) + 1;
    static_assert(
        int(LoomUtility::TransitionEase) == UtilityCount + 2,
        "A utility added after LoomUtility::Shadow must either map to a "
        "property path (declare it before Shadow) or be handled before "
        "LoomTargetProfile::propertyPath() is reached.");
    QString m_paths[UtilityCount];
    bool m_lineHeight = false;
};
