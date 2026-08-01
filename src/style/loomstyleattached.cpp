#include "loomstyleattached.h"

#include <QLoggingCategory>
#include <QMetaProperty>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickWindow>
#include <QSet>
#include <QVector2D>

#include "loomstatewatcher.h"
#include "loomtargetprofile.h"
#include "loomtokenregistry.h"

Q_STATIC_LOGGING_CATEGORY(lcLoomApply, "loom.style")

namespace {

// `bg-surface/70`. Scales the token's own alpha rather than replacing it, so a
// colour that is already translucent composes with the modifier instead of
// being overridden by it.
QColor withAlpha(QColor color, quint8 alphaPercent)
{
    if (alphaPercent == 100 || !color.isValid())
        return color;
    color.setAlphaF(color.alphaF() * (alphaPercent / 100.0f));
    return color;
}

void warnUnsupportedOnce(const QMetaObject *type, LoomUtility utility, const QString &key)
{
    static QSet<QPair<const QMetaObject *, quint8>> warned;
    const auto entry = qMakePair(type, quint8(utility));
    if (warned.contains(entry))
        return;
    warned.insert(entry);
    // `key` is empty for every flag utility (italic, underline, hidden, ...),
    // so naming the family is what makes the warning actionable at all.
    qCWarning(lcLoomApply).noquote()
        << "Lo.style: utility" << loomUtilityName(utility)
        << (key.isEmpty() ? QString() : QStringLiteral("(%1)").arg(key))
        << "is not supported on" << type->className()
        << "- see docs/styling/limitations.md";
}

// Distinct from warnUnsupportedOnce: the utility is fine and the type supports
// it, the item is just on the wrong side of the anchors/layout divide. Claiming
// "not supported on QQuickRectangle" would send someone after the wrong problem
// -- `pin-l` is perfectly supported on a Rectangle, just not inside a layout.
void warnLayoutMismatchOnce(
    const QMetaObject *type, LoomUtility utility, bool requiresLayout)
{
    static QSet<QPair<const QMetaObject *, quint8>> warned;
    const auto entry = qMakePair(type, quint8(utility));
    if (warned.contains(entry))
        return;
    warned.insert(entry);
    if (requiresLayout) {
        qCWarning(lcLoomApply).noquote()
            << "Lo.style: utility" << loomUtilityName(utility)
            << "only applies inside a RowLayout, ColumnLayout or GridLayout;"
            << type->className() << "is not in one";
        return;
    }
    qCWarning(lcLoomApply).noquote()
        << "Lo.style: utility" << loomUtilityName(utility)
        << "has no form inside a layout, which places" << type->className()
        << "itself - use self-start/self-center/self-end instead";
}

void warnInvalidPathOnce(const QMetaObject *type, const QString &path)
{
    static QSet<QPair<const QMetaObject *, QString>> warned;
    const auto entry = qMakePair(type, path);
    if (warned.contains(entry))
        return;
    warned.insert(entry);
    qCWarning(lcLoomApply).noquote()
        << "Lo.style: cannot resolve" << path << "on" << type->className()
        << (path.startsWith(QLatin1String("Layout."))
                ? "- import QtQuick.Layouts in the file that declares the item"
                : "- property missing");
}

// What one rule wants to write: 1..3 property paths with resolved values.
struct ResolvedWrite {
    QString path;
    QVariant value;
};

} // namespace

LoomStyleAttached::LoomStyleAttached(QObject *parent)
    : QObject(parent)
{
    m_target = qobject_cast<QQuickItem *>(parent);
    if (!m_target) {
        qCWarning(lcLoomApply) << "Lo.style is only meaningful on Items, not"
                               << (parent ? parent->metaObject()->className() : "null");
        return;
    }
    // Token values can change under any style (theme switch, config load).
    connect(
        LoomTokenRegistry::instance(), &LoomTokenRegistry::tokensChanged, this,
        &LoomStyleAttached::scheduleApply);
    // A config load can also change which token names *exist*. m_compiled was
    // produced when they did not, so it is missing every rule that named one;
    // re-applying it would faithfully re-apply that gap. The compile cache is
    // cleared by the loader, so this recompiles from scratch. tokensChanged
    // follows and applies the result.
    connect(
        LoomTokenRegistry::instance(), &LoomTokenRegistry::vocabularyChanged, this,
        &LoomStyleAttached::recompile);
}

void LoomStyleAttached::recompile()
{
    if (!m_target || m_style.isEmpty())
        return;
    m_compiled = LoomStyleCompiler::compile(m_style);
    updateSubscriptions();
}

QString LoomStyleAttached::style() const
{
    return m_style;
}

void LoomStyleAttached::setStyle(const QString &style)
{
    if (style == m_style)
        return;
    m_style = style;
    m_compiled = m_target ? LoomStyleCompiler::compile(style) : nullptr;
    if (m_target)
        updateSubscriptions();
    emit styleChanged();
    scheduleApply();
}

void LoomStyleAttached::scheduleApply()
{
    if (m_applyQueued || !m_target)
        return;
    m_applyQueued = true;
    // Queued on purpose, twice over: it coalesces bursts (theme switch plus
    // resize plus hover in one turn is one apply), and it defers the first
    // apply past object creation, so Lo.style wins over the item's own initial
    // property assignments regardless of their order in the document.
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_applyQueued = false;
            applyNow();
        },
        Qt::QueuedConnection);
}

bool LoomStyleAttached::connectPropertyNotify(QObject *sender, const char *propertyName)
{
    const QMetaObject *mo = sender->metaObject();
    const int index = mo->indexOfProperty(propertyName);
    if (index < 0)
        return false;
    const QMetaProperty property = mo->property(index);
    if (!property.hasNotifySignal())
        return false;
    static const QMetaMethod slot =
        staticMetaObject.method(staticMetaObject.indexOfSlot("scheduleApply()"));
    return bool(connect(sender, property.notifySignal(), this, slot));
}

void LoomStyleAttached::updateSubscriptions()
{
    const quint8 states = m_compiled ? m_compiled->usedStates : 0;

    if ((states & (LoomHoverState | LoomPressedState)) && !m_watcher) {
        m_nativePressed = false;
        if (states & LoomPressedState) {
            // A target with its own NOTIFYing bool `pressed` (MouseArea,
            // Controls buttons, custom components) is the better source: it
            // reflects keyboard presses and the target's own gesture policy.
            const int index = m_target->metaObject()->indexOfProperty("pressed");
            if (index >= 0
                && m_target->metaObject()->property(index).metaType()
                    == QMetaType::fromType<bool>())
                m_nativePressed = connectPropertyNotify(m_target, "pressed");
        }
        const bool needWatcher = (states & LoomHoverState)
            || ((states & LoomPressedState) && !m_nativePressed);
        if (needWatcher) {
            m_watcher = loomCreateStateWatcher(m_target, this);
            if (m_watcher) {
                connectPropertyNotify(m_watcher, "hovered");
                connectPropertyNotify(m_watcher, "pressed");
            }
        }
    }

    if (states & LoomFocusState)
        connect(
            m_target, &QQuickItem::activeFocusChanged, this,
            &LoomStyleAttached::scheduleApply, Qt::UniqueConnection);
    if (states & LoomDisabledState)
        connect(
            m_target, &QQuickItem::enabledChanged, this,
            &LoomStyleAttached::scheduleApply, Qt::UniqueConnection);

    if (m_compiled && m_compiled->usesBreakpoints) {
        connect(
            m_target, &QQuickItem::windowChanged, this, &LoomStyleAttached::trackWindow,
            Qt::UniqueConnection);
        trackWindow();
    }
    if (m_compiled && m_compiled->usesParentSize) {
        connect(
            m_target, &QQuickItem::parentChanged, this, &LoomStyleAttached::trackParent,
            Qt::UniqueConnection);
        trackParent();
    }
    // Margins and the layout family resolve against the parent's type (Layout
    // vs. anchors), so a reparent re-routes them.
    if (m_compiled && (m_compiled->usesMargins || m_compiled->usesLayout))
        connect(
            m_target, &QQuickItem::parentChanged, this, &LoomStyleAttached::scheduleApply,
            Qt::UniqueConnection);
    // aspect-* derives height from the item's own width. Writing height raises
    // heightChanged, not widthChanged, so this cannot feed back on itself.
    if (m_compiled && m_compiled->usesAspect)
        connect(
            m_target, &QQuickItem::widthChanged, this, &LoomStyleAttached::scheduleApply,
            Qt::UniqueConnection);
}

void LoomStyleAttached::trackWindow()
{
    disconnect(m_windowWidthConn);
    if (QQuickWindow *window = m_target->window())
        m_windowWidthConn = connect(
            window, &QWindow::widthChanged, this, &LoomStyleAttached::scheduleApply);
    scheduleApply();
}

void LoomStyleAttached::trackParent()
{
    disconnect(m_parentSizeConnW);
    disconnect(m_parentSizeConnH);
    if (QQuickItem *parent = m_target->parentItem()) {
        m_parentSizeConnW = connect(
            parent, &QQuickItem::widthChanged, this, &LoomStyleAttached::scheduleApply);
        m_parentSizeConnH = connect(
            parent, &QQuickItem::heightChanged, this, &LoomStyleAttached::scheduleApply);
    }
    scheduleApply();
}

quint8 LoomStyleAttached::activeStates() const
{
    quint8 states = 0;
    if (m_watcher && m_watcher->property("hovered").toBool())
        states |= LoomHoverState;
    const bool pressed = m_nativePressed
        ? m_target->property("pressed").toBool()
        : (m_watcher && m_watcher->property("pressed").toBool());
    if (pressed)
        states |= LoomPressedState;
    if (m_target->hasActiveFocus())
        states |= LoomFocusState;
    if (!m_target->isEnabled())
        states |= LoomDisabledState;
    if (LoomTokenRegistry::instance()->isDark())
        states |= LoomDarkState;
    return states;
}

QString LoomStyleAttached::marginPath(LoomUtility utility) const
{
    const char *side = nullptr;
    switch (utility) {
    case LoomUtility::MarginTop:
        side = "topMargin";
        break;
    case LoomUtility::MarginRight:
        side = "rightMargin";
        break;
    case LoomUtility::MarginBottom:
        side = "bottomMargin";
        break;
    default:
        side = "leftMargin";
        break;
    }
    const QQuickItem *parent = m_target->parentItem();
    if (parent && loomInheritsByName(parent->metaObject(), "QQuickLayout"))
        return QStringLiteral("Layout.") + QLatin1String(side);
    return QStringLiteral("anchors.") + QLatin1String(side);
}

// Layout utilities land in one of two worlds, decided by the parent's type.
// Anchoring an item that a layout manages is undefined behaviour Qt warns
// about, so this is not a convenience: writing the anchors form inside a layout
// would be actively wrong. Resolved per apply, so a reparent re-routes.
LoomStyleAttached::LayoutPaths
LoomStyleAttached::layoutPaths(LoomUtility utility, LayoutMismatch *mismatch) const
{
    *mismatch = LayoutMismatch::None;
    const QQuickItem *parent = m_target->parentItem();
    const bool inLayout =
        parent && loomInheritsByName(parent->metaObject(), "QQuickLayout");

    const auto anchors = [](const char *name) {
        return LayoutPaths{QStringLiteral("anchors.") + QLatin1String(name)};
    };
    const auto layout = [](const char *name) {
        return LayoutPaths{QStringLiteral("Layout.") + QLatin1String(name)};
    };

    switch (utility) {
    case LoomUtility::AnchorFill:
        if (inLayout)
            return {
                QStringLiteral("Layout.fillWidth"), QStringLiteral("Layout.fillHeight")};
        return anchors("fill");
    case LoomUtility::AnchorFillX:
        if (inLayout)
            return layout("fillWidth");
        return {QStringLiteral("anchors.left"), QStringLiteral("anchors.right")};
    case LoomUtility::AnchorFillY:
        if (inLayout)
            return layout("fillHeight");
        return {QStringLiteral("anchors.top"), QStringLiteral("anchors.bottom")};
    case LoomUtility::AnchorCenter:
        // A layout has no "centre in the parent"; centring is an alignment.
        return inLayout ? layout("alignment") : anchors("centerIn");

    // Single-axis centring and edge pins have no layout equivalent that does
    // not fight the layout's own placement. `self-*` is the answer there.
    case LoomUtility::AnchorCenterX:
    case LoomUtility::AnchorCenterY:
    case LoomUtility::AnchorPinTop:
    case LoomUtility::AnchorPinRight:
    case LoomUtility::AnchorPinBottom:
    case LoomUtility::AnchorPinLeft:
        if (inLayout) {
            *mismatch = LayoutMismatch::NoLayoutForm;
            return {};
        }
        switch (utility) {
        case LoomUtility::AnchorCenterX:
            return anchors("horizontalCenter");
        case LoomUtility::AnchorCenterY:
            return anchors("verticalCenter");
        case LoomUtility::AnchorPinTop:
            return anchors("top");
        case LoomUtility::AnchorPinRight:
            return anchors("right");
        case LoomUtility::AnchorPinBottom:
            return anchors("bottom");
        default:
            return anchors("left");
        }

    // Layout-only: Qt Quick has no min/max or span concept off a layout.
    case LoomUtility::LayoutAlignment:
    case LoomUtility::LayoutMinWidth:
    case LoomUtility::LayoutMaxWidth:
    case LoomUtility::LayoutMinHeight:
    case LoomUtility::LayoutMaxHeight:
    case LoomUtility::LayoutColumnSpan:
    case LoomUtility::LayoutRowSpan:
        if (!inLayout) {
            *mismatch = LayoutMismatch::RequiresLayout;
            return {};
        }
        switch (utility) {
        case LoomUtility::LayoutAlignment:
            return layout("alignment");
        case LoomUtility::LayoutMinWidth:
            return layout("minimumWidth");
        case LoomUtility::LayoutMaxWidth:
            return layout("maximumWidth");
        case LoomUtility::LayoutMinHeight:
            return layout("minimumHeight");
        case LoomUtility::LayoutMaxHeight:
            return layout("maximumHeight");
        case LoomUtility::LayoutColumnSpan:
            return layout("columnSpan");
        default:
            return layout("rowSpan");
        }

    case LoomUtility::AspectRatio:
        // A layout owns its children's geometry, so state a preference rather
        // than writing height under it.
        return inLayout ? layout("preferredHeight")
                        : LayoutPaths{QStringLiteral("height")};

    default:
        return {};
    }
}

// `parent.left` as a value. QQuickItem exposes its anchor lines through
// Q_PRIVATE_PROPERTY, so they are in the metaobject and resolvable by name,
// and QQuickAnchorLine is a registered metatype -- which means the value can be
// carried in a QVariant and written straight into the target's anchors group
// without loom ever naming the private type or linking a private header.
QVariant LoomStyleAttached::parentAnchorLine(const QString &edge) const
{
    QQuickItem *parent = m_target->parentItem();
    if (!parent)
        return {};
    return QQmlProperty(parent, edge, qmlContext(m_target)).read();
}

// A Control is not a Rectangle: it paints its box through a `background`
// delegate, so bg-*/rounded*/border* have nothing to land on and used to warn.
// Route them to that delegate when the delegate itself supports them. Duck-typed
// on the property name, like p-* and gap-*, so any type exposing an Item
// `background` opts in rather than only QtQuick.Controls -- loom does not depend
// on Controls and cannot name its types. Resolved per instance rather than
// folded into the cached per-type profile, because the delegate is replaceable
// per instance and differs between Controls styles.
QString LoomStyleAttached::backgroundPath(LoomUtility utility) const
{
    switch (utility) {
    case LoomUtility::BgColor:
    case LoomUtility::Radius:
    case LoomUtility::RadiusTopLeft:
    case LoomUtility::RadiusTopRight:
    case LoomUtility::RadiusBottomRight:
    case LoomUtility::RadiusBottomLeft:
    case LoomUtility::BorderWidth:
    case LoomUtility::BorderColor:
        break;
    default:
        // Everything else already lands on the control itself: p-* and gap-*
        // are its own properties, text-*/font-* belong to its contentItem.
        return {};
    }

    const QVariant value = m_target->property("background");
    if (!value.isValid())
        return {};
    const auto *background = value.value<QQuickItem *>();
    if (!background)
        return {};
    const QString path =
        LoomTargetProfile::forType(background->metaObject())->propertyPath(utility);
    if (path.isEmpty())
        return {};
    return QStringLiteral("background.") + path;
}

int LoomStyleAttached::breakpointTier() const
{
    QQuickWindow *window = m_target->window();
    if (!window)
        return 0;
    const int width = window->width();
    auto *registry = LoomTokenRegistry::instance();
    static const char *const names[] = {"sm", "md", "lg", "xl"};
    int tier = 0;
    for (int i = 0; i < 4; ++i) {
        // Stop at the first threshold the width does not reach. Continuing
        // would let a later, smaller threshold in a non-monotonic config
        // promote the tier past a breakpoint whose own threshold is unmet.
        if (width < registry->breakpoint(QLatin1String(names[i])))
            break;
        tier = i + 1;
    }
    return tier;
}

void LoomStyleAttached::applyNow()
{
    if (!m_target)
        return;

    struct Desired {
        QVariant value;
        quint8 specificity;
    };
    QHash<QString, Desired> desired;
    QString shadowKey;
    quint8 shadowSpecificity = 0;
    // Tracking is em-relative, so it can only be resolved once the winning
    // text size for this pass is known. Deferred rather than computed in the
    // rule loop, where it silently used the item's pre-existing pixel size
    // unless a `text-{size}` class happened to appear earlier in the string.
    struct {
        QString key;
        QString path;
        quint8 specificity = 0;
        bool set = false;
    } tracking;
    // Tailwind defaults: 150ms, cubic-bezier(0.4, 0, 0.2, 1).
    struct {
        LoomTransitionMode mode = LoomTransitionMode::None;
        QString durationKey = QStringLiteral("150");
        QString easeKey = QStringLiteral("in-out");
        quint8 modeSpecificity = 0;
        quint8 durationSpecificity = 0;
        quint8 easeSpecificity = 0;
    } transition;

    if (m_compiled) {
        auto *registry = LoomTokenRegistry::instance();
        const LoomTargetProfile *profile =
            LoomTargetProfile::forType(m_target->metaObject());
        const quint8 states = activeStates();
        const int tier = breakpointTier();

        for (const LoomStyleRule &rule : m_compiled->rules) {
            if (rule.minBreakpoint > tier)
                continue;
            if ((rule.stateMask & states) != rule.stateMask)
                continue;

            if (rule.utility == LoomUtility::Shadow) {
                // Not a property write: applied through the managed effect
                // item after the write pass. Same specificity ranking.
                if (rule.specificity >= shadowSpecificity) {
                    shadowKey = rule.key;
                    shadowSpecificity = rule.specificity;
                }
                continue;
            }
            if (rule.utility == LoomUtility::TransitionMode) {
                if (rule.specificity >= transition.modeSpecificity) {
                    transition.mode = LoomTransitionMode(quint8(rule.literal));
                    transition.modeSpecificity = rule.specificity;
                }
                continue;
            }
            if (rule.utility == LoomUtility::TransitionDuration) {
                if (rule.specificity >= transition.durationSpecificity) {
                    transition.durationKey = rule.key;
                    transition.durationSpecificity = rule.specificity;
                }
                continue;
            }
            if (rule.utility == LoomUtility::TransitionEase) {
                if (rule.specificity >= transition.easeSpecificity) {
                    transition.easeKey = rule.key;
                    transition.easeSpecificity = rule.specificity;
                }
                continue;
            }

            QString path = profile->propertyPath(rule.utility);
            if (path.isEmpty())
                path = backgroundPath(rule.utility);
            if (path.isEmpty()) {
                warnUnsupportedOnce(m_target->metaObject(), rule.utility, rule.key);
                continue;
            }

            QVarLengthArray<ResolvedWrite, 3> writes;
            switch (rule.utility) {
            case LoomUtility::BgColor:
            case LoomUtility::TextColor:
            case LoomUtility::BorderColor:
                writes.append(
                    {path, withAlpha(registry->color(rule.key), rule.alphaPercent)});
                break;
            case LoomUtility::TextSize: {
                const LoomTextStyle size = registry->textSize(rule.key);
                writes.append({path, size.size});
                if (profile->supportsLineHeight()) {
                    // 1 == Text.FixedHeight; the enum only exists in QML.
                    writes.append({QStringLiteral("lineHeight"), size.lineHeight});
                    writes.append({QStringLiteral("lineHeightMode"), 1});
                }
                break;
            }
            case LoomUtility::FontWeight:
                writes.append({path, registry->fontWeight(rule.key)});
                break;
            case LoomUtility::Italic:
            case LoomUtility::Underline:
            case LoomUtility::LineThrough:
            case LoomUtility::Visible:
                writes.append({path, rule.flag});
                break;
            case LoomUtility::Tracking:
                // Deferred to after the loop, where the winning text size for
                // this pass is known regardless of class order.
                if (rule.specificity >= tracking.specificity) {
                    tracking.key = rule.key;
                    tracking.path = path;
                    tracking.specificity = rule.specificity;
                    tracking.set = true;
                }
                break;
            case LoomUtility::PaddingTop:
            case LoomUtility::PaddingRight:
            case LoomUtility::PaddingBottom:
            case LoomUtility::PaddingLeft:
            case LoomUtility::Gap:
            case LoomUtility::Width:
            case LoomUtility::Height:
                writes.append({path, registry->space(rule.key)});
                break;
            case LoomUtility::MarginTop:
            case LoomUtility::MarginRight:
            case LoomUtility::MarginBottom:
            case LoomUtility::MarginLeft:
                // Inside a Layout the Layout.* attached margins are the ones
                // that do anything; anchor margins otherwise. Resolved per
                // apply so reparenting between the two worlds re-routes.
                writes.append({marginPath(rule.utility), registry->space(rule.key)});
                break;
            case LoomUtility::WidthFull:
            case LoomUtility::HeightFull: {
                QQuickItem *parent = m_target->parentItem();
                if (!parent)
                    break;
                writes.append(
                    {path,
                     rule.utility == LoomUtility::WidthFull ? parent->width()
                                                            : parent->height()});
                break;
            }
            case LoomUtility::Radius:
            case LoomUtility::RadiusTopLeft:
            case LoomUtility::RadiusTopRight:
            case LoomUtility::RadiusBottomRight:
            case LoomUtility::RadiusBottomLeft:
                writes.append({path, registry->radius(rule.key)});
                break;
            case LoomUtility::BorderWidth:
                writes.append({path, rule.literal});
                break;
            case LoomUtility::Opacity:
                writes.append({path, registry->opacityValue(rule.key)});
                break;
            case LoomUtility::AnchorFill:
            case LoomUtility::AnchorFillX:
            case LoomUtility::AnchorFillY:
            case LoomUtility::AnchorCenter:
            case LoomUtility::AnchorCenterX:
            case LoomUtility::AnchorCenterY:
            case LoomUtility::AnchorPinTop:
            case LoomUtility::AnchorPinRight:
            case LoomUtility::AnchorPinBottom:
            case LoomUtility::AnchorPinLeft:
            case LoomUtility::LayoutAlignment:
            case LoomUtility::LayoutMinWidth:
            case LoomUtility::LayoutMaxWidth:
            case LoomUtility::LayoutMinHeight:
            case LoomUtility::LayoutMaxHeight:
            case LoomUtility::LayoutColumnSpan:
            case LoomUtility::LayoutRowSpan:
            case LoomUtility::AspectRatio: {
                // The path from the profile is only the support gate; where the
                // write actually lands depends on this item's parent, so it is
                // resolved here per apply. Same shape as m-* above.
                LayoutMismatch mismatch = LayoutMismatch::None;
                const LayoutPaths paths = layoutPaths(rule.utility, &mismatch);
                if (paths.isEmpty()) {
                    if (mismatch != LayoutMismatch::None)
                        warnLayoutMismatchOnce(
                            m_target->metaObject(), rule.utility,
                            mismatch == LayoutMismatch::RequiresLayout);
                    else
                        warnUnsupportedOnce(
                            m_target->metaObject(), rule.utility, rule.key);
                    break;
                }
                QQuickItem *parent = m_target->parentItem();
                for (const QString &target : paths) {
                    if (target == QLatin1String("anchors.fill")
                        || target == QLatin1String("anchors.centerIn")) {
                        if (!parent)
                            break;
                        writes.append({target, QVariant::fromValue(parent)});
                    } else if (target.startsWith(QLatin1String("anchors."))) {
                        const QVariant line =
                            parentAnchorLine(target.mid(qstrlen("anchors.")));
                        if (!line.isValid())
                            break;
                        writes.append({target, line});
                    } else if (
                        target == QLatin1String("Layout.fillWidth")
                        || target == QLatin1String("Layout.fillHeight")) {
                        writes.append({target, true});
                    } else if (target == QLatin1String("Layout.alignment")) {
                        // `center` in a layout means centred; `self-*` carries
                        // its own alignment in the rule.
                        const auto alignment = rule.utility == LoomUtility::AnchorCenter
                            ? Qt::Alignment(Qt::AlignCenter)
                            : Qt::Alignment(Qt::AlignmentFlag(int(rule.literal)));
                        writes.append({target, QVariant::fromValue(alignment)});
                    } else if (
                        rule.utility == LoomUtility::LayoutColumnSpan
                        || rule.utility == LoomUtility::LayoutRowSpan) {
                        writes.append({target, int(rule.literal)});
                    } else if (rule.utility == LoomUtility::AspectRatio) {
                        if (rule.literal > 0)
                            writes.append({target, m_target->width() / rule.literal});
                    } else {
                        writes.append({target, registry->space(rule.key)});
                    }
                }
                break;
            }
            case LoomUtility::Shadow:
            case LoomUtility::TransitionMode:
            case LoomUtility::TransitionDuration:
            case LoomUtility::TransitionEase:
                // Handled before the switch; not property writes.
                break;
            }

            for (const ResolvedWrite &write : writes) {
                // Specificity: states outrank breakpoints, and either outranks
                // an unqualified rule; at equal rank the later rule wins
                // (iteration order). See loomSpecificity().
                const auto existing = desired.constFind(write.path);
                if (existing != desired.constEnd()
                    && existing->specificity > rule.specificity)
                    continue;
                desired.insert(write.path, {write.value, rule.specificity});
            }
        }

        // Em-relative, so it resolves against the size this pass is setting --
        // whether that class came before or after the tracking one -- and falls
        // back to the item's current size when the pass sets none.
        if (tracking.set) {
            qreal pixelSize =
                QQmlProperty(m_target, QStringLiteral("font.pixelSize")).read().toReal();
            if (const auto sized = desired.constFind(QStringLiteral("font.pixelSize"));
                sized != desired.constEnd())
                pixelSize = sized->value.toReal();
            desired.insert(
                tracking.path,
                {registry->tracking(tracking.key) * pixelSize, tracking.specificity});
        }
    }

    QQmlContext *context = qmlContext(m_target);

    // Release properties no longer managed, restoring their pre-Loom values.
    for (auto it = m_lastWritten.begin(); it != m_lastWritten.end();) {
        if (desired.contains(it.key())) {
            ++it;
            continue;
        }
        stopAnimation(it.key());
        QQmlProperty property(m_target, it.key(), context);
        const QVariant original = m_originals.take(it.key());
        // Writing the saved value back is right whenever it is assignable, and
        // for an anchor that was unset before Loom touched it, it is not: a
        // default anchor line names no item, and Qt refuses it. Falling back to
        // reset() releases the anchor instead of leaving the item stuck to it.
        // Type-agnostic on purpose -- QQuickAnchorLine is private, so the saved
        // value can only be moved around opaquely, never inspected.
        if (!property.write(original))
            property.reset();
        it = m_lastWritten.erase(it);
    }

    for (auto it = desired.constBegin(); it != desired.constEnd(); ++it) {
        QQmlProperty property(m_target, it.key(), context);
        if (!property.isValid()) {
            // Reachable for Layout.* margins when the target's document does
            // not import QtQuick.Layouts; attached types resolve through the
            // file's imports.
            warnInvalidPathOnce(m_target->metaObject(), it.key());
            continue;
        }
        const auto last = m_lastWritten.constFind(it.key());
        const bool firstWrite = last == m_lastWritten.constEnd();
        if (firstWrite)
            m_originals.insert(it.key(), property.read());
        else if (*last == it->value)
            continue;
        // First writes snap: animating in from whatever the item happened to
        // start as would make creation itself a transition.
        if (!firstWrite && transitionCovers(transition.mode, it.key(), it->value)) {
            animateWrite(
                property, it.key(), it->value,
                LoomTokenRegistry::instance()->duration(transition.durationKey),
                LoomTokenRegistry::instance()->easing(transition.easeKey));
        } else {
            stopAnimation(it.key());
            property.write(it->value);
        }
        m_lastWritten.insert(it.key(), it->value);
    }

    syncShadow(shadowKey);
}

bool LoomStyleAttached::transitionCovers(
    LoomTransitionMode mode, const QString &path, const QVariant &value)
{
    const bool isColor = value.typeId() == QMetaType::QColor;
    const bool isOpacity = path == QLatin1String("opacity");
    switch (mode) {
    case LoomTransitionMode::None:
        return false;
    case LoomTransitionMode::Default:
        return isColor || isOpacity;
    case LoomTransitionMode::Colors:
        return isColor;
    case LoomTransitionMode::Opacity:
        return isOpacity;
    case LoomTransitionMode::All:
        return isColor || value.typeId() == QMetaType::Double;
    }
    return false;
}

void LoomStyleAttached::stopAnimation(const QString &path)
{
    if (QVariantAnimation *animation = m_animations.take(path))
        animation->stop(); // DeleteWhenStopped
}

void LoomStyleAttached::animateWrite(
    const QQmlProperty &property, const QString &path, const QVariant &target,
    int durationMs, const QEasingCurve &curve)
{
    stopAnimation(path);
    auto *animation = new QVariantAnimation(this);
    // Starting from the current read means an interrupted animation retargets
    // smoothly instead of jumping.
    animation->setStartValue(property.read());
    animation->setEndValue(target);
    animation->setDuration(durationMs);
    animation->setEasingCurve(curve);
    connect(
        animation, &QVariantAnimation::valueChanged, this,
        [property](const QVariant &value) { property.write(value); });
    m_animations.insert(path, animation);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void LoomStyleAttached::syncShadow(const QString &shadowKey)
{
    const bool wantShadow = !shadowKey.isEmpty() && shadowKey != QLatin1String("none");
    if (!wantShadow) {
        if (m_shadowItem) {
            // deleteLater, not delete: the effect's internals clean up
            // through the event loop; a synchronous delete leaks them.
            m_shadowItem->setParentItem(nullptr);
            m_shadowItem->deleteLater();
            m_shadowItem.clear();
        }
        return;
    }
    if (!m_shadowItem && !m_shadowFailed) {
        m_shadowItem = loomCreateShadowItem(m_target, this);
        m_shadowFailed = m_shadowItem.isNull();
    }
    if (!m_shadowItem)
        return;

    const LoomShadow shadow = LoomTokenRegistry::instance()->shadow(shadowKey);
    m_shadowItem->setProperty("color", shadow.color);
    m_shadowItem->setProperty("blur", shadow.blur);
    m_shadowItem->setProperty("spread", shadow.spread);
    m_shadowItem->setProperty(
        "offset", QVector2D(float(shadow.offsetX), float(shadow.offsetY)));
}

LoomStyleAttached *Lo::qmlAttachedProperties(QObject *object)
{
    return new LoomStyleAttached(object);
}
