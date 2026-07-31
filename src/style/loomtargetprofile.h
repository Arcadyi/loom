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

    static constexpr int UtilityCount = int(LoomUtility::Shadow) + 1;
    QString m_paths[UtilityCount];
    bool m_lineHeight = false;
};
