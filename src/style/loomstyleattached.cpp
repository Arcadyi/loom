#include "loomstyleattached.h"

#include <QAccessibilityHints>
#include <QEvent>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QMetaProperty>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlListReference>
#include <QQmlProperty>
#include <QQuickWindow>
#include <QSet>
#include <QStyleHints>
#include <QVector2D>
#include <QtQml/qqml.h>
#include <algorithm>
#include <utility>

#include "loomstatewatcher.h"
#include "loomtargetprofile.h"
#include "loomtokenregistry.h"

Q_STATIC_LOGGING_CATEGORY(lcLoomApply, "loom.style")

namespace {

// `bg-surface/70`. Scales the token's own alpha rather than replacing it, so a
// colour that is already translucent composes with the modifier instead of
// being overridden by it.
// The property a declared state duck-types onto. State names are kebab-case,
// matching the variant vocabulary (`not-found:`); QML properties are camelCase
// (`notFound`). Doing the conversion in one place keeps the two spellings from
// drifting into a mismatch nothing would report.
QByteArray loomStatePropertyName(const QString &state)
{
    QByteArray name;
    name.reserve(state.size());
    bool capitalise = false;
    for (const QChar character : state) {
        if (character == QLatin1Char('-')) {
            capitalise = true;
            continue;
        }
        name.append(
            (capitalise ? character.toUpper() : character).toLatin1());
        capitalise = false;
    }
    return name;
}

QColor withAlpha(QColor color, quint8 alphaPercent)
{
    if (alphaPercent == 100 || !color.isValid())
        return color;
    color.setAlphaF(color.alphaF() * (alphaPercent / 100.0f));
    return color;
}

bool activeThemeHasToken(const LoomStyleRule &rule, const LoomTokenRegistry *registry)
{
    if (rule.arbitrary || rule.key.isEmpty())
        return true;
    switch (rule.utility) {
    case LoomUtility::BgColor:
    case LoomUtility::TextColor:
    case LoomUtility::BorderColor:
    case LoomUtility::RingColor:
    case LoomUtility::GradientFrom:
    case LoomUtility::GradientVia:
    case LoomUtility::GradientTo:
        return registry->hasColor(rule.key);
    case LoomUtility::TextSize:
    case LoomUtility::LineHeight:
        return registry->hasTextSize(rule.key);
    case LoomUtility::FontWeight:
        return registry->hasFontWeight(rule.key);
    case LoomUtility::FontFamily:
        return registry->hasFontFamily(rule.key);
    case LoomUtility::Tracking:
        return registry->hasTracking(rule.key);
    case LoomUtility::Radius:
    case LoomUtility::RadiusTopLeft:
    case LoomUtility::RadiusTopRight:
    case LoomUtility::RadiusBottomRight:
    case LoomUtility::RadiusBottomLeft:
        return registry->hasRadius(rule.key);
    case LoomUtility::Opacity:
        return registry->hasOpacityValue(rule.key);
    case LoomUtility::Shadow:
        return registry->hasShadow(rule.key);
    case LoomUtility::TransitionDuration:
        return registry->hasDuration(rule.key);
    case LoomUtility::TransitionEase:
        return registry->hasEasing(rule.key);
    case LoomUtility::PaddingTop:
    case LoomUtility::PaddingRight:
    case LoomUtility::PaddingBottom:
    case LoomUtility::PaddingLeft:
    case LoomUtility::MarginTop:
    case LoomUtility::MarginRight:
    case LoomUtility::MarginBottom:
    case LoomUtility::MarginLeft:
    case LoomUtility::Gap:
    case LoomUtility::Width:
    case LoomUtility::Height:
    case LoomUtility::LayoutMinWidth:
    case LoomUtility::LayoutMaxWidth:
    case LoomUtility::LayoutMinHeight:
    case LoomUtility::LayoutMaxHeight:
    case LoomUtility::TranslateX:
    case LoomUtility::TranslateY:
        return registry->hasSpace(rule.key);
    default:
        return true;
    }
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

class InputModalityTracker final : public QObject {
public:
    static InputModalityTracker *instance()
    {
        static InputModalityTracker tracker;
        return &tracker;
    }

    void subscribe(QObject *object)
    {
        for (const auto &subscriber : std::as_const(m_subscribers)) {
            if (subscriber == object)
                return;
        }
        m_subscribers.append(object);
    }

    bool keyboard() const
    {
        return m_keyboard;
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        Q_UNUSED(watched)
        bool keyboard = m_keyboard;
        switch (event->type()) {
        case QEvent::KeyPress:
        case QEvent::Shortcut:
            keyboard = true;
            break;
        case QEvent::MouseButtonPress:
        case QEvent::TouchBegin:
        case QEvent::TabletPress:
            keyboard = false;
            break;
        default:
            return false;
        }
        if (keyboard == m_keyboard)
            return false;
        m_keyboard = keyboard;
        for (auto it = m_subscribers.begin(); it != m_subscribers.end();) {
            if (!*it) {
                it = m_subscribers.erase(it);
                continue;
            }
            QMetaObject::invokeMethod(*it, "scheduleApply", Qt::QueuedConnection);
            ++it;
        }
        return false;
    }

private:
    InputModalityTracker()
    {
        if (QCoreApplication::instance())
            QCoreApplication::instance()->installEventFilter(this);
    }

    QList<QPointer<QObject>> m_subscribers;
    bool m_keyboard = true;
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
    connect(
        LoomTokenRegistry::instance(), &LoomTokenRegistry::accessibilityChanged, this,
        &LoomStyleAttached::scheduleApply);
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

bool LoomStyleAttached::container() const
{
    return m_container;
}

void LoomStyleAttached::setContainer(bool container)
{
    if (m_container == container)
        return;
    m_container = container;
    emit contextChanged();
    scheduleApply();
}

QString LoomStyleAttached::containerName() const
{
    return m_containerName;
}

void LoomStyleAttached::setContainerName(const QString &name)
{
    if (m_containerName == name)
        return;
    m_containerName = name;
    emit contextChanged();
    scheduleApply();
}

QString LoomStyleAttached::group() const
{
    return m_group;
}

void LoomStyleAttached::setGroup(const QString &name)
{
    if (m_group == name)
        return;
    m_group = name;
    emit contextChanged();
    scheduleApply();
}

bool LoomStyleAttached::effects() const
{
    return m_effects;
}

void LoomStyleAttached::setEffects(bool enabled)
{
    if (m_effects == enabled)
        return;
    m_effects = enabled;
    emit effectsChanged();
    scheduleApply();
}

QVariantMap LoomStyleAttached::states() const
{
    return m_stateMap;
}

void LoomStyleAttached::setStates(const QVariantMap &states)
{
    m_stateMap = states;
    quint32 resolved = 0;
    auto *registry = LoomTokenRegistry::instance();
    for (auto it = states.constBegin(); it != states.constEnd(); ++it) {
        const int bit = registry->customStateBit(it.key());
        if (bit < 0) {
            // Warn rather than ignore: a state that was never declared can
            // never match a variant either, so silently accepting it would
            // leave `invalid:border-danger` compiling and doing nothing, with
            // no way to tell from the outside which half is wrong.
            qCWarning(lcLoomApply).noquote()
                << "Lo.states: undeclared state" << it.key()
                << "-- add it to the design file's \"states\" block";
            continue;
        }
        if (it.value().toBool())
            resolved |= quint32(1) << bit;
    }
    emit statesChanged();
    // Only on a real change. The map is typically a binding that re-evaluates
    // whenever any of its expressions do, so most writes resolve to the same
    // mask and must not cost an apply.
    if (resolved == m_declaredStates)
        return;
    m_declaredStates = resolved;
    updateSubscriptions();
    scheduleApply();
}

// Declared states have two sources, and a component can use either. The map is
// the explicit one; a plain `property bool invalid` on the target is picked up
// the same way `checked` and `readOnly` already are, which is what lets a
// component light up its own states without every call site restating them.
quint32 LoomStyleAttached::activeCustomStates() const
{
    quint32 states = m_declaredStates;
    if (!m_target)
        return states;
    const QStringList names = LoomTokenRegistry::instance()->customStateNames();
    for (qsizetype bit = 0; bit < names.size(); ++bit) {
        const quint32 mask = quint32(1) << bit;
        if (states & mask)
            continue;
        const QVariant value = m_target->property(loomStatePropertyName(names.at(bit)));
        if (value.isValid() && value.toBool())
            states |= mask;
    }
    return states;
}

bool LoomStyleAttached::matchesGroup(
    const QString &name, quint32 required, quint32 forbidden, quint32 customRequired,
    quint32 customForbidden) const
{
    if ((!name.isEmpty() && m_group != name) || (name.isEmpty() && m_group.isEmpty()))
        return false;
    const quint32 states = activeStates();
    if ((states & required) != required || (states & forbidden) != 0)
        return false;
    const quint32 custom = activeCustomStates();
    return (custom & customRequired) == customRequired && (custom & customForbidden) == 0;
}

void LoomStyleAttached::subscribeExternalStates(quint32 states)
{
    const quint32 combined = m_externalStates | states;
    if (combined == m_externalStates)
        return;
    m_externalStates = combined;
    updateSubscriptions();
}

// A group host whose own style never mentions a declared state still has to
// watch it, because a descendant's `group-invalid:` reads it from here. Only
// matters for the duck-typed source: a host supplying values through Lo.states
// already schedules an apply on every change, and scheduleApply() emits
// contextChanged, which is what descendants are connected to.
void LoomStyleAttached::subscribeExternalCustomStates(quint32 states)
{
    const quint32 combined = m_externalCustomStates | states;
    if (combined == m_externalCustomStates)
        return;
    m_externalCustomStates = combined;
    updateSubscriptions();
}

QVariantMap LoomStyleAttached::debugInfo() const
{
    QVariantMap resolved;
    for (auto it = m_lastWritten.constBegin(); it != m_lastWritten.constEnd(); ++it)
        resolved.insert(it.key(), it.value());

    QStringList states;
    const quint32 active = m_target ? activeStates() : 0;
    const struct {
        quint32 state;
        const char *name;
    } names[] = {
        {LoomHoverState, "hover"},
        {LoomPressedState, "pressed"},
        {LoomFocusState, "focus"},
        {LoomDisabledState, "disabled"},
        {LoomDarkState, "dark"},
        {LoomCheckedState, "checked"},
        {LoomDownState, "down"},
        {LoomHighlightedState, "highlighted"},
        {LoomSelectedState, "selected"},
        {LoomEditableState, "editable"},
        {LoomReadOnlyState, "read-only"},
        {LoomActiveState, "active"},
        {LoomFocusWithinState, "focus-within"},
        {LoomFocusVisibleState, "focus-visible"},
        {LoomRtlState, "rtl"},
        {LoomLtrState, "ltr"},
        {LoomPortraitState, "portrait"},
        {LoomLandscapeState, "landscape"},
        {LoomWindowActiveState, "window-active"},
        {LoomHighContrastState, "high-contrast"},
        {LoomMotionReduceState, "motion-reduce"},
        {LoomFirstState, "first"},
        {LoomLastState, "last"},
        {LoomOnlyState, "only"},
        {LoomOddState, "odd"},
        {LoomEvenState, "even"},
    };
    for (const auto &entry : names) {
        if (active & entry.state)
            states.append(QLatin1String(entry.name));
    }

    return {
        {QStringLiteral("type"),
         m_target ? QString::fromLatin1(m_target->metaObject()->className()) : QString()},
        {QStringLiteral("objectName"), m_target ? m_target->objectName() : QString()},
        {QStringLiteral("style"), m_style},
        {QStringLiteral("theme"), LoomTokenRegistry::instance()->theme()},
        {QStringLiteral("states"), states},
        {QStringLiteral("resolved"), resolved},
        {QStringLiteral("ruleCount"), m_compiled ? m_compiled->rules.size() : 0},
        {QStringLiteral("container"), m_container},
        {QStringLiteral("containerName"), m_containerName},
        {QStringLiteral("group"), m_group},
        {QStringLiteral("effects"), m_effects},
    };
}

void LoomStyleAttached::scheduleApply()
{
    if (m_applyQueued || !m_target)
        return;
    m_applyQueued = true;
    emit contextChanged();
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

void LoomStyleAttached::refreshContextSubscriptions()
{
    // New siblings and newly selected container/group ancestors were not
    // present in the old subscription set. Refresh it before recomputing.
    updateSubscriptions();
    scheduleApply();
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
    const quint32 states = (m_compiled ? m_compiled->usedStates : 0) | m_externalStates;

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

    const struct {
        quint32 state;
        const char *property;
    } propertyStates[] = {
        {LoomCheckedState, "checked"},         {LoomDownState, "down"},
        {LoomHighlightedState, "highlighted"}, {LoomSelectedState, "selected"},
        {LoomEditableState, "editable"},       {LoomReadOnlyState, "readOnly"},
        {LoomActiveState, "active"},           {LoomInvalidState, "invalid"},
    };
    for (const auto &entry : propertyStates) {
        if (states & entry.state)
            connectPropertyNotify(m_target, entry.property);
    }

    // Declared states the style actually asks about, sourced from a property on
    // the target. connectPropertyNotify is a no-op when the target has no such
    // property, which is the common case: most call sites supply the values
    // through Lo.states instead, and that needs no subscription because the map
    // is already a QML binding.
    if (const quint32 customStates =
            (m_compiled ? m_compiled->usedCustomStates : quint32(0))
            | m_externalCustomStates) {
        const QStringList names = LoomTokenRegistry::instance()->customStateNames();
        for (qsizetype bit = 0; bit < names.size(); ++bit) {
            if (customStates & (quint32(1) << bit))
                connectPropertyNotify(m_target, loomStatePropertyName(names.at(bit)));
        }
    }
    if (states & LoomFocusVisibleState) {
        InputModalityTracker::instance()->subscribe(this);
        if (!connectPropertyNotify(m_target, "visualFocus"))
            connect(
                m_target, &QQuickItem::activeFocusChanged, this,
                &LoomStyleAttached::scheduleApply, Qt::UniqueConnection);
    }
    if (states & (LoomRtlState | LoomLtrState)) {
        if (auto *application =
                qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
            connect(
                application, &QGuiApplication::layoutDirectionChanged, this,
                &LoomStyleAttached::scheduleApply, Qt::UniqueConnection);
    }
    if (states & LoomHighContrastState) {
        if (auto *hints = QGuiApplication::styleHints()->accessibility()) {
            connect(
                hints, &QAccessibilityHints::contrastPreferenceChanged, this,
                &LoomStyleAttached::scheduleApply, Qt::UniqueConnection);
        }
    }
    if (states
        & (LoomFirstState | LoomLastState | LoomOnlyState | LoomOddState
           | LoomEvenState)) {
        // Lo.style is assigned before a declarative child is inserted into its
        // visual parent. Defer the subscription refresh until that insertion
        // has completed; otherwise structural variants created in QML never
        // observe later sibling additions.
        connect(
            m_target, &QQuickItem::parentChanged, this,
            &LoomStyleAttached::refreshContextSubscriptions,
            Qt::ConnectionType(Qt::QueuedConnection | Qt::UniqueConnection));
        if (QQuickItem *parent = m_target->parentItem()) {
            connect(
                parent, &QQuickItem::childrenChanged, this,
                &LoomStyleAttached::refreshContextSubscriptions, Qt::UniqueConnection);
            for (QQuickItem *sibling : parent->childItems()) {
                connect(
                    sibling, &QQuickItem::visibleChanged, this,
                    &LoomStyleAttached::scheduleApply, Qt::UniqueConnection);
            }
        }
    }

    if (m_compiled
        && (m_compiled->usesBreakpoints
            || (states
                & (LoomPortraitState | LoomLandscapeState | LoomWindowActiveState
                   | LoomFocusWithinState)))) {
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
    if (m_compiled && m_compiled->usesTranslate) {
        connect(
            m_target, &QQuickItem::widthChanged, this, &LoomStyleAttached::scheduleApply,
            Qt::UniqueConnection);
        connect(
            m_target, &QQuickItem::heightChanged, this, &LoomStyleAttached::scheduleApply,
            Qt::UniqueConnection);
    }
    if (m_compiled && (m_compiled->usesContainers || m_compiled->usesGroups)) {
        connect(
            m_target, &QQuickItem::parentChanged, this,
            &LoomStyleAttached::refreshContextSubscriptions,
            Qt::ConnectionType(Qt::QueuedConnection | Qt::UniqueConnection));
        // Observe every already-attached ancestor, not only the one matching
        // right now. A bound Lo.group/Lo.containerName can turn a previously
        // irrelevant ancestor into the selected context without reparenting.
        for (QQuickItem *ancestor = m_target->parentItem(); ancestor;
             ancestor = ancestor->parentItem()) {
            if (auto *attached = qobject_cast<LoomStyleAttached *>(
                    qmlAttachedPropertiesObject<Lo>(ancestor, false))) {
                connect(
                    attached, &LoomStyleAttached::contextChanged, this,
                    &LoomStyleAttached::refreshContextSubscriptions,
                    Qt::UniqueConnection);
            }
        }
        QSet<QString> containerNames;
        QSet<QString> groupNames;
        for (const auto &rule : m_compiled->rules) {
            if (rule.containerMinWidth > 0
                || rule.containerMaxWidth != std::numeric_limits<int>::max())
                containerNames.insert(rule.containerName);
            if (rule.groupStateMask || rule.groupStateNotMask || rule.groupCustomMask
                || rule.groupCustomNotMask)
                groupNames.insert(rule.groupName);
        }
        for (const QString &name : containerNames) {
            if (QQuickItem *item = containerItem(name)) {
                connect(
                    item, &QQuickItem::widthChanged, this,
                    &LoomStyleAttached::scheduleApply, Qt::UniqueConnection);
            }
        }
        for (const QString &name : groupNames) {
            if (auto *attached = groupContext(name)) {
                quint32 required = 0;
                quint32 customRequired = 0;
                for (const auto &rule : m_compiled->rules) {
                    if (rule.groupName != name)
                        continue;
                    required |= rule.groupStateMask | rule.groupStateNotMask;
                    customRequired |= rule.groupCustomMask | rule.groupCustomNotMask;
                }
                attached->subscribeExternalStates(required);
                attached->subscribeExternalCustomStates(customRequired);
            }
        }
    }
}

void LoomStyleAttached::trackWindow()
{
    disconnect(m_windowWidthConn);
    disconnect(m_windowHeightConn);
    disconnect(m_windowActiveConn);
    disconnect(m_windowFocusConn);
    if (QQuickWindow *window = m_target->window()) {
        m_windowWidthConn = connect(
            window, &QWindow::widthChanged, this, &LoomStyleAttached::scheduleApply);
        m_windowHeightConn = connect(
            window, &QWindow::heightChanged, this, &LoomStyleAttached::scheduleApply);
        m_windowActiveConn = connect(
            window, &QWindow::activeChanged, this, &LoomStyleAttached::scheduleApply);
        m_windowFocusConn = connect(
            window, &QQuickWindow::activeFocusItemChanged, this,
            &LoomStyleAttached::scheduleApply);
    }
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

quint32 LoomStyleAttached::activeStates() const
{
    quint32 states = 0;
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
    const auto boolProperty = [this](const char *name) {
        const QVariant value = m_target->property(name);
        return value.isValid() && value.toBool();
    };
    if (boolProperty("checked"))
        states |= LoomCheckedState;
    if (boolProperty("down"))
        states |= LoomDownState;
    if (boolProperty("highlighted"))
        states |= LoomHighlightedState;
    if (boolProperty("selected"))
        states |= LoomSelectedState;
    if (boolProperty("editable"))
        states |= LoomEditableState;
    if (boolProperty("readOnly"))
        states |= LoomReadOnlyState;
    if (boolProperty("active"))
        states |= LoomActiveState;
    if (boolProperty("invalid"))
        states |= LoomInvalidState;
    if (boolProperty("visualFocus")
        || (m_target->metaObject()->indexOfProperty("visualFocus") < 0
            && m_target->hasActiveFocus()
            && InputModalityTracker::instance()->keyboard()))
        states |= LoomFocusVisibleState;
    if (QGuiApplication::layoutDirection() == Qt::RightToLeft)
        states |= LoomRtlState;
    else
        states |= LoomLtrState;
    if (QQuickWindow *window = m_target->window()) {
        if (window->width() >= window->height())
            states |= LoomLandscapeState;
        else
            states |= LoomPortraitState;
        if (window->isActive())
            states |= LoomWindowActiveState;
        for (QQuickItem *focus = window->activeFocusItem(); focus;
             focus = focus->parentItem()) {
            if (focus == m_target) {
                states |= LoomFocusWithinState;
                break;
            }
        }
    }
    if (QGuiApplication::styleHints()->accessibility()
        && QGuiApplication::styleHints()->accessibility()->contrastPreference()
            == Qt::ContrastPreference::HighContrast)
        states |= LoomHighContrastState;
    if (LoomTokenRegistry::instance()->reduceMotion())
        states |= LoomMotionReduceState;
    if (QQuickItem *parent = m_target->parentItem()) {
        QList<QQuickItem *> siblings;
        for (QQuickItem *sibling : parent->childItems()) {
            if (sibling->isVisible() && !sibling->property("_loomInternal").toBool())
                siblings.append(sibling);
        }
        const qsizetype index = siblings.indexOf(m_target);
        if (index >= 0) {
            if (index == 0)
                states |= LoomFirstState;
            if (index == siblings.size() - 1)
                states |= LoomLastState;
            if (siblings.size() == 1)
                states |= LoomOnlyState;
            if ((index + 1) % 2 == 1)
                states |= LoomOddState;
            else
                states |= LoomEvenState;
        }
    }
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
// The mirror of backgroundPath() for the label half of a Control. A Control
// carries `font`, so text-sm and font-bold already land on it and Qt propagates
// them down; `color` is the gap, because a Control is not a Text and the target
// profile is duck-typed on the property, not on intent. Without this,
// `text-white` on a Button warns as unsupported and the label keeps the
// platform style's colour against whatever background bg-* just wrote.
QString LoomStyleAttached::contentPath(LoomUtility utility) const
{
    switch (utility) {
    case LoomUtility::TextColor:
    case LoomUtility::TextAlignment:
    case LoomUtility::TextElide:
    case LoomUtility::LineHeight:
        break;
    default:
        // Everything else either lands on the Control (padding, spacing, the
        // whole font group) or is already delegated to its background.
        return {};
    }

    const QVariant value = m_target->property("contentItem");
    if (!value.isValid())
        return {};
    const auto *content = value.value<QQuickItem *>();
    if (!content)
        return {};
    const QString path =
        LoomTargetProfile::forType(content->metaObject())->propertyPath(utility);
    if (path.isEmpty())
        return {};
    return QStringLiteral("contentItem.") + path;
}

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

QQuickItem *LoomStyleAttached::containerItem(const QString &name) const
{
    for (QQuickItem *item = m_target ? m_target->parentItem() : nullptr; item;
         item = item->parentItem()) {
        auto *attached = qobject_cast<LoomStyleAttached *>(
            qmlAttachedPropertiesObject<Lo>(item, false));
        if (!attached || !attached->container())
            continue;
        if (name.isEmpty() || attached->containerName() == name)
            return item;
    }
    return nullptr;
}

LoomStyleAttached *LoomStyleAttached::groupContext(const QString &name) const
{
    for (QQuickItem *item = m_target ? m_target->parentItem() : nullptr; item;
         item = item->parentItem()) {
        auto *attached = qobject_cast<LoomStyleAttached *>(
            qmlAttachedPropertiesObject<Lo>(item, false));
        if (!attached || attached->group().isEmpty())
            continue;
        if (name.isEmpty() || attached->group() == name)
            return attached;
    }
    return nullptr;
}

void LoomStyleAttached::applyNow()
{
    if (!m_target)
        return;

    struct Desired {
        QVariant value;
        quint64 specificity;
    };
    QHash<QString, Desired> desired;
    QString shadowKey;
    quint64 shadowSpecificity = 0;
    int cursorShape = int(Qt::ArrowCursor);
    quint64 cursorSpecificity = 0;
    bool cursorSet = false;
    qreal translateX = 0;
    qreal translateY = 0;
    quint64 translateXSpecificity = 0;
    quint64 translateYSpecificity = 0;
    bool translateXSet = false;
    bool translateYSet = false;
    qreal ringWidth = 0;
    QColor ringColor;
    quint64 ringWidthSpecificity = 0;
    quint64 ringColorSpecificity = 0;
    bool ringWidthSet = false;
    int gradientDirection = 4;
    QColor gradientFrom;
    QColor gradientVia;
    QColor gradientTo;
    quint64 gradientDirectionSpecificity = 0;
    quint64 gradientFromSpecificity = 0;
    quint64 gradientViaSpecificity = 0;
    quint64 gradientToSpecificity = 0;
    bool gradientDirectionSet = false;
    qreal filterBlur = 0;
    qreal filterBrightness = 100;
    qreal filterContrast = 100;
    qreal filterSaturation = 100;
    quint64 filterBlurSpecificity = 0;
    quint64 filterBrightnessSpecificity = 0;
    quint64 filterContrastSpecificity = 0;
    quint64 filterSaturationSpecificity = 0;
    bool filterSet = false;
    // Tracking is em-relative, so it can only be resolved once the winning
    // text size for this pass is known. Deferred rather than computed in the
    // rule loop, where it silently used the item's pre-existing pixel size
    // unless a `text-{size}` class happened to appear earlier in the string.
    struct {
        QString key;
        QString path;
        qreal literal = 0;
        quint64 specificity = 0;
        bool arbitrary = false;
        bool set = false;
    } tracking;
    // Tailwind defaults: 150ms, cubic-bezier(0.4, 0, 0.2, 1).
    struct {
        LoomTransitionMode mode = LoomTransitionMode::None;
        QString durationKey = QStringLiteral("150");
        QString easeKey = QStringLiteral("in-out");
        quint64 modeSpecificity = 0;
        quint64 durationSpecificity = 0;
        quint64 easeSpecificity = 0;
    } transition;

    if (m_compiled) {
        auto *registry = LoomTokenRegistry::instance();
        const LoomTargetProfile *profile =
            LoomTargetProfile::forType(m_target->metaObject());
        const quint32 states = activeStates();
        const quint32 customStates = activeCustomStates();
        const int viewportWidth = m_target->window() ? m_target->window()->width() : 0;

        for (const LoomStyleRule &rule : m_compiled->rules) {
            if (rule.minWidth > viewportWidth || rule.maxWidth < viewportWidth)
                continue;
            if (rule.containerMinWidth > 0
                || rule.containerMaxWidth != std::numeric_limits<int>::max()) {
                QQuickItem *container = containerItem(rule.containerName);
                if (!container || container->width() < rule.containerMinWidth
                    || container->width() > rule.containerMaxWidth)
                    continue;
            }
            if ((rule.stateMask & states) != rule.stateMask)
                continue;
            if ((rule.stateNotMask & states) != 0)
                continue;
            if ((rule.customMask & customStates) != rule.customMask)
                continue;
            if ((rule.customNotMask & customStates) != 0)
                continue;
            if (!rule.themeName.isEmpty() && registry->theme() != rule.themeName)
                continue;
            if (rule.groupStateMask || rule.groupStateNotMask || rule.groupCustomMask
                || rule.groupCustomNotMask) {
                const auto *group = groupContext(rule.groupName);
                if (!group
                    || !group->matchesGroup(
                        rule.groupName, rule.groupStateMask, rule.groupStateNotMask,
                        rule.groupCustomMask, rule.groupCustomNotMask))
                    continue;
            }
            if (!activeThemeHasToken(rule, registry))
                continue;

            const auto colorFor = [registry, &rule] {
                return withAlpha(
                    rule.arbitrary ? QColor::fromString(rule.key)
                                   : registry->color(rule.key),
                    rule.alphaPercent);
            };

            if (rule.utility == LoomUtility::CursorShape) {
                if (rule.specificity >= cursorSpecificity) {
                    cursorShape = int(rule.literal);
                    cursorSpecificity = rule.specificity;
                    cursorSet = true;
                }
                continue;
            }
            if (rule.utility == LoomUtility::TranslateX
                || rule.utility == LoomUtility::TranslateY) {
                qreal value = rule.arbitrary ? rule.literal : registry->space(rule.key);
                if (rule.fraction > 0) {
                    value = (rule.utility == LoomUtility::TranslateX ? m_target->width()
                                                                     : m_target->height())
                        * rule.fraction;
                }
                if (rule.negative)
                    value = -value;
                qreal &target =
                    rule.utility == LoomUtility::TranslateX ? translateX : translateY;
                quint64 &specificity = rule.utility == LoomUtility::TranslateX
                    ? translateXSpecificity
                    : translateYSpecificity;
                bool &set = rule.utility == LoomUtility::TranslateX ? translateXSet
                                                                    : translateYSet;
                if (rule.specificity >= specificity) {
                    target = value;
                    specificity = rule.specificity;
                    set = true;
                }
                continue;
            }

            if (rule.utility == LoomUtility::Shadow) {
                // Not a property write: applied through the managed effect
                // item after the write pass. Same specificity ranking.
                if (rule.specificity >= shadowSpecificity) {
                    shadowKey = rule.key;
                    shadowSpecificity = rule.specificity;
                }
                continue;
            }
            if (rule.utility == LoomUtility::RingWidth) {
                if (rule.specificity >= ringWidthSpecificity) {
                    ringWidth = rule.literal;
                    ringWidthSpecificity = rule.specificity;
                    ringWidthSet = true;
                }
                continue;
            }
            if (rule.utility == LoomUtility::RingColor) {
                if (rule.specificity >= ringColorSpecificity) {
                    ringColor = colorFor();
                    ringColorSpecificity = rule.specificity;
                }
                continue;
            }
            if (rule.utility == LoomUtility::GradientDirection) {
                if (rule.specificity >= gradientDirectionSpecificity) {
                    gradientDirection = int(rule.literal);
                    gradientDirectionSpecificity = rule.specificity;
                    gradientDirectionSet = true;
                }
                continue;
            }
            if (rule.utility == LoomUtility::GradientFrom
                || rule.utility == LoomUtility::GradientVia
                || rule.utility == LoomUtility::GradientTo) {
                QColor *target = rule.utility == LoomUtility::GradientFrom ? &gradientFrom
                    : rule.utility == LoomUtility::GradientVia             ? &gradientVia
                                                                           : &gradientTo;
                quint64 *specificity = rule.utility == LoomUtility::GradientFrom
                    ? &gradientFromSpecificity
                    : rule.utility == LoomUtility::GradientVia ? &gradientViaSpecificity
                                                               : &gradientToSpecificity;
                if (rule.specificity >= *specificity) {
                    *target = colorFor();
                    *specificity = rule.specificity;
                }
                continue;
            }
            if (rule.utility >= LoomUtility::FilterBlur
                && rule.utility <= LoomUtility::FilterSaturation) {
                qreal *target = nullptr;
                quint64 *specificity = nullptr;
                switch (rule.utility) {
                case LoomUtility::FilterBlur:
                    target = &filterBlur;
                    specificity = &filterBlurSpecificity;
                    break;
                case LoomUtility::FilterBrightness:
                    target = &filterBrightness;
                    specificity = &filterBrightnessSpecificity;
                    break;
                case LoomUtility::FilterContrast:
                    target = &filterContrast;
                    specificity = &filterContrastSpecificity;
                    break;
                case LoomUtility::FilterSaturation:
                    target = &filterSaturation;
                    specificity = &filterSaturationSpecificity;
                    break;
                default:
                    break;
                }
                if (target && rule.specificity >= *specificity) {
                    *target = rule.literal;
                    *specificity = rule.specificity;
                    filterSet = true;
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
            if (path.isEmpty())
                path = contentPath(rule.utility);
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
                    {path,
                     withAlpha(
                         rule.arbitrary ? QColor::fromString(rule.key)
                                        : registry->color(rule.key),
                         rule.alphaPercent)});
                break;
            case LoomUtility::TextSize: {
                const LoomTextStyle size = rule.arbitrary
                    ? LoomTextStyle{rule.literal, rule.literal * 1.5}
                    : registry->textSize(rule.key);
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
            case LoomUtility::FontFamily:
                writes.append(
                    {path,
                     rule.arbitrary ? rule.key
                                    : registry->fontFamily(rule.key).value(0)});
                break;
            case LoomUtility::TextAlignment:
            case LoomUtility::TextElide:
            case LoomUtility::TextMaximumLines:
            case LoomUtility::TextCapitalization:
            case LoomUtility::TextWrapMode:
            case LoomUtility::TransformOrigin:
                writes.append({path, int(rule.literal)});
                break;
            case LoomUtility::LineHeight:
                writes.append(
                    {path,
                     rule.arbitrary ? rule.literal
                                    : registry->textSize(rule.key).lineHeight});
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
                    tracking.literal = rule.literal;
                    tracking.specificity = rule.specificity;
                    tracking.arbitrary = rule.arbitrary;
                    tracking.set = true;
                }
                break;
            case LoomUtility::PaddingTop:
            case LoomUtility::PaddingRight:
            case LoomUtility::PaddingBottom:
            case LoomUtility::PaddingLeft:
            case LoomUtility::Gap:
            case LoomUtility::Width:
            case LoomUtility::Height: {
                qreal value = rule.arbitrary ? rule.literal : registry->space(rule.key);
                if (rule.fraction > 0) {
                    if (QQuickItem *parent = m_target->parentItem())
                        value = (rule.utility == LoomUtility::Width ? parent->width()
                                                                    : parent->height())
                            * rule.fraction;
                }
                writes.append({path, rule.negative ? -value : value});
                break;
            }
            case LoomUtility::MarginTop:
            case LoomUtility::MarginRight:
            case LoomUtility::MarginBottom:
            case LoomUtility::MarginLeft:
                // Inside a Layout the Layout.* attached margins are the ones
                // that do anything; anchor margins otherwise. Resolved per
                // apply so reparenting between the two worlds re-routes.
                {
                    const qreal value =
                        rule.arbitrary ? rule.literal : registry->space(rule.key);
                    writes.append(
                        {marginPath(rule.utility), rule.negative ? -value : value});
                }
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
                writes.append(
                    {path, rule.arbitrary ? rule.literal : registry->radius(rule.key)});
                break;
            case LoomUtility::BorderWidth:
                writes.append({path, rule.literal});
                break;
            case LoomUtility::Opacity:
                writes.append(
                    {path,
                     rule.arbitrary ? rule.literal : registry->opacityValue(rule.key)});
                break;
            case LoomUtility::Clip:
                writes.append({path, rule.flag});
                break;
            case LoomUtility::ZOrder:
            case LoomUtility::Rotation:
            case LoomUtility::Scale:
                writes.append({path, rule.negative ? -rule.literal : rule.literal});
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
                        writes.append(
                            {target,
                             rule.arbitrary ? rule.literal : registry->space(rule.key)});
                    }
                }
                break;
            }
            case LoomUtility::Shadow:
            case LoomUtility::CursorShape:
            case LoomUtility::TranslateX:
            case LoomUtility::TranslateY:
            case LoomUtility::RingWidth:
            case LoomUtility::RingColor:
            case LoomUtility::GradientDirection:
            case LoomUtility::GradientFrom:
            case LoomUtility::GradientVia:
            case LoomUtility::GradientTo:
            case LoomUtility::FilterBlur:
            case LoomUtility::FilterBrightness:
            case LoomUtility::FilterContrast:
            case LoomUtility::FilterSaturation:
            case LoomUtility::TransitionMode:
            case LoomUtility::TransitionDuration:
            case LoomUtility::TransitionEase:
                // Handled before the switch; not property writes.
                break;
            }

            for (const ResolvedWrite &write : writes) {
                // Specificity: states outrank responsive constraints, and
                // either outranks an unqualified rule; at equal rank the later
                // rule wins (iteration order). See loomSpecificity().
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
                {(tracking.arbitrary ? tracking.literal
                                     : registry->tracking(tracking.key))
                     * pixelSize,
                 tracking.specificity});
        }
        if (registry->reduceMotion())
            transition.mode = LoomTransitionMode::None;
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

    syncCursor(cursorShape, cursorSet);
    syncTranslate(translateX, translateY, translateXSet || translateYSet);
    syncShadow(shadowKey);
    syncRing(
        ringWidth,
        ringColor.isValid()
            ? ringColor
            : LoomTokenRegistry::instance()->color(QStringLiteral("accent")),
        ringWidthSet && ringWidth > 0);
    syncGradient(
        gradientDirection, gradientFrom, gradientVia, gradientTo,
        gradientDirectionSet && gradientFrom.isValid());
    syncFilters(
        filterBlur, filterBrightness, filterContrast, filterSaturation, filterSet);
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

void LoomStyleAttached::syncCursor(int shape, bool enabled)
{
#if QT_CONFIG(cursor)
    if (!enabled) {
        if (m_cursorManaged) {
            m_target->setCursor(m_originalCursor);
            m_cursorManaged = false;
        }
        return;
    }
    if (!m_cursorManaged) {
        m_originalCursor = m_target->cursor();
        m_cursorManaged = true;
    }
    m_target->setCursor(QCursor(Qt::CursorShape(shape)));
#else
    Q_UNUSED(shape)
    Q_UNUSED(enabled)
#endif
}

void LoomStyleAttached::syncTranslate(qreal x, qreal y, bool enabled)
{
    if (!enabled) {
        if (m_translate)
            m_translate->deleteLater();
        m_translate.clear();
        return;
    }
    if (!m_translate) {
        QQmlEngine *engine = qmlEngine(m_target);
        if (!engine) {
            qCWarning(lcLoomApply)
                << "Lo.style: translate-* needs an item created by a QML engine";
            return;
        }
        QQmlComponent component(engine);
        component.setData("import QtQuick\nTranslate {}", QUrl());
        QObject *translate = component.create();
        if (!translate) {
            qCWarning(lcLoomApply)
                << "Lo.style: could not create Translate:" << component.errorString();
            return;
        }
        QQmlListReference transforms(m_target, "transform");
        if (!transforms.isValid() || !transforms.append(translate)) {
            qCWarning(lcLoomApply)
                << "Lo.style: could not append Translate to Item.transform";
            translate->deleteLater();
            return;
        }
        translate->setParent(this);
        m_translate = translate;
    }
    m_translate->setProperty("x", x);
    m_translate->setProperty("y", y);
}

void LoomStyleAttached::syncRing(qreal width, const QColor &color, bool enabled)
{
    if (!enabled) {
        if (m_ringItem) {
            m_ringItem->setParentItem(nullptr);
            m_ringItem->deleteLater();
        }
        m_ringItem.clear();
        return;
    }
    if (!m_ringItem && !m_ringFailed) {
        m_ringItem = loomCreateRingItem(m_target, this);
        m_ringFailed = m_ringItem.isNull();
    }
    if (!m_ringItem)
        return;
    QQmlProperty(m_ringItem, QStringLiteral("border.width")).write(width);
    QQmlProperty(m_ringItem, QStringLiteral("border.color")).write(color);
}

void LoomStyleAttached::syncGradient(
    int direction, const QColor &from, const QColor &via, const QColor &to, bool enabled)
{
    const auto release = [this] {
        if (!m_gradient)
            return;
        QQmlProperty property(m_target, m_gradientPath, qmlContext(m_target));
        if (property.isValid()) {
            if (m_originalGradient.isNull())
                property.reset();
            else if (!property.write(m_originalGradient))
                property.reset();
        }
        m_gradient->deleteLater();
        m_gradient.clear();
        m_gradientPath.clear();
        m_originalGradient.clear();
    };
    if (!enabled) {
        release();
        return;
    }

    const LoomTargetProfile *profile = LoomTargetProfile::forType(m_target->metaObject());
    QString colorPath = profile->propertyPath(LoomUtility::BgColor);
    if (colorPath.isEmpty())
        colorPath = backgroundPath(LoomUtility::BgColor);
    const QString path = colorPath == QLatin1String("color") ? QStringLiteral("gradient")
        : colorPath == QLatin1String("background.color")
        ? QStringLiteral("background.gradient")
        : QString();
    if (path.isEmpty()) {
        warnUnsupportedOnce(
            m_target->metaObject(), LoomUtility::GradientDirection, QString());
        release();
        return;
    }

    if (m_gradient && m_gradientPath != path)
        release();
    if (!m_gradient && !m_gradientFailed) {
        QQmlEngine *engine = qmlEngine(m_target);
        if (!engine) {
            qCWarning(lcLoomApply) << "Lo.style: gradient utilities need a QML engine";
            m_gradientFailed = true;
            return;
        }
        QQmlComponent component(engine);
        component.setData(
            R"(import QtQuick
Gradient {
    id: root
    property color loomFrom
    property color loomVia
    property color loomTo
    property bool loomHorizontal
    property bool loomReverse
    orientation: loomHorizontal ? Gradient.Horizontal : Gradient.Vertical
    GradientStop { position: 0; color: root.loomReverse ? root.loomTo : root.loomFrom }
    GradientStop { position: 0.5; color: root.loomVia }
    GradientStop { position: 1; color: root.loomReverse ? root.loomFrom : root.loomTo }
})",
            QUrl());
        QObject *gradient = component.create();
        if (!gradient) {
            qCWarning(lcLoomApply)
                << "Lo.style: could not create Gradient:" << component.errorString();
            m_gradientFailed = true;
            return;
        }
        gradient->setParent(this);
        QQmlProperty property(m_target, path, qmlContext(m_target));
        m_originalGradient = property.read();
        if (!property.write(QVariant::fromValue(gradient))) {
            qCWarning(lcLoomApply) << "Lo.style: could not assign" << path;
            gradient->deleteLater();
            m_gradientFailed = true;
            return;
        }
        m_gradient = gradient;
        m_gradientPath = path;
    }
    if (!m_gradient)
        return;

    const QColor actualTo =
        to.isValid() ? to : QColor(from.red(), from.green(), from.blue(), 0);
    QColor actualVia = via;
    if (!actualVia.isValid()) {
        actualVia = QColor::fromRgbF(
            (from.redF() + actualTo.redF()) / 2, (from.greenF() + actualTo.greenF()) / 2,
            (from.blueF() + actualTo.blueF()) / 2,
            (from.alphaF() + actualTo.alphaF()) / 2);
    }
    // Rectangle gradients are axis-aligned. Diagonal directions select their
    // dominant horizontal axis, preserving stop order and deterministic output.
    const bool horizontal = direction == 1 || direction == 2 || direction == 3
        || direction == 5 || direction == 6 || direction == 7;
    const bool reverse = direction == 0 || direction == 6 || direction == 7;
    m_gradient->setProperty("loomFrom", from);
    m_gradient->setProperty("loomVia", actualVia);
    m_gradient->setProperty("loomTo", actualTo);
    m_gradient->setProperty("loomHorizontal", horizontal);
    m_gradient->setProperty("loomReverse", reverse);
}

void LoomStyleAttached::syncFilters(
    qreal blurPixels, qreal brightnessPercent, qreal contrastPercent,
    qreal saturationPercent, bool enabled)
{
    const auto release = [this] {
        if (!m_filterManaged)
            return;
        QQmlProperty effect(
            m_target, QStringLiteral("layer.effect"), qmlContext(m_target));
        QQmlProperty layerEnabled(
            m_target, QStringLiteral("layer.enabled"), qmlContext(m_target));
        if (effect.isValid()) {
            if (m_originalLayerEffect.isNull())
                effect.reset();
            else if (!effect.write(m_originalLayerEffect))
                effect.reset();
        }
        if (layerEnabled.isValid())
            layerEnabled.write(m_originalLayerEnabled);
        if (m_filterComponent)
            m_filterComponent->deleteLater();
        m_filterComponent.clear();
        m_filterManaged = false;
        m_filterSignature.clear();
        m_originalLayerEffect.clear();
        m_originalLayerEnabled.clear();
    };
    if (!enabled || !m_effects) {
        if (enabled && !m_effects) {
            static QSet<const QMetaObject *> warned;
            if (!warned.contains(m_target->metaObject())) {
                warned.insert(m_target->metaObject());
                qCWarning(lcLoomApply)
                    << "Lo.style: blur/brightness/contrast/saturate require "
                       "Lo.effects: true because they own Item.layer.effect";
            }
        }
        release();
        return;
    }

    QQmlEngine *engine = qmlEngine(m_target);
    if (!engine || m_filterFailed)
        return;
    const qreal blur = std::clamp(blurPixels / 64.0, 0.0, 1.0);
    const qreal brightness = std::clamp((brightnessPercent - 100.0) / 100.0, -1.0, 1.0);
    const qreal contrast = std::clamp((contrastPercent - 100.0) / 100.0, -1.0, 1.0);
    const qreal saturation = std::clamp((saturationPercent - 100.0) / 100.0, -1.0, 1.0);
    const QString signature = QStringLiteral("%1;%2;%3;%4")
                                  .arg(blur, 0, 'g', 12)
                                  .arg(brightness, 0, 'g', 12)
                                  .arg(contrast, 0, 'g', 12)
                                  .arg(saturation, 0, 'g', 12);
    if (m_filterManaged && signature == m_filterSignature)
        return;
    QQmlProperty effect(m_target, QStringLiteral("layer.effect"), qmlContext(m_target));
    QQmlProperty layerEnabled(
        m_target, QStringLiteral("layer.enabled"), qmlContext(m_target));
    if (!effect.isValid() || !layerEnabled.isValid()) {
        qCWarning(lcLoomApply) << "Lo.style: Item.layer is unavailable on" << m_target;
        m_filterFailed = true;
        return;
    }
    if (!m_filterManaged) {
        // Item.layer.effect stores a Component, not an already-created effect.
        // A QQmlComponent assembled directly with setData() has no creation
        // context, and QQuickItemLayer dereferences that null context when it
        // enables the layer. Create a small host object in the target's QML
        // context instead; its nested Component then carries the lexical
        // context that QQuickItemLayer requires.
        static const QByteArray source = QByteArrayLiteral(
            "import QtQuick\n"
            "import QtQuick.Effects\n"
            "QtObject {\n"
            "    id: host\n"
            "    property real loomBlur: 0\n"
            "    property real loomBrightness: 0\n"
            "    property real loomContrast: 0\n"
            "    property real loomSaturation: 0\n"
            "    property Component loomEffect: Component {\n"
            "        MultiEffect {\n"
            "            blurEnabled: host.loomBlur > 0\n"
            "            blurMax: 64\n"
            "            blur: host.loomBlur\n"
            "            brightness: host.loomBrightness\n"
            "            contrast: host.loomContrast\n"
            "            saturation: host.loomSaturation\n"
            "        }\n"
            "    }\n"
            "}\n");
        QQmlComponent definition(engine);
        definition.setData(source, QUrl());
        if (definition.isError()) {
            qCWarning(lcLoomApply)
                << "Lo.style: could not define MultiEffect:" << definition.errorString();
            m_filterFailed = true;
            return;
        }
        QObject *host = definition.create(qmlContext(m_target));
        if (!host) {
            qCWarning(lcLoomApply) << "Lo.style: could not create MultiEffect host:"
                                   << definition.errorString();
            m_filterFailed = true;
            return;
        }
        host->setParent(this);
        auto *effectComponent = host->property("loomEffect").value<QQmlComponent *>();
        if (!effectComponent) {
            qCWarning(lcLoomApply) << "Lo.style: MultiEffect host has no component";
            host->deleteLater();
            m_filterFailed = true;
            return;
        }
        m_originalLayerEffect = effect.read();
        m_originalLayerEnabled = layerEnabled.read();
        m_filterComponent = host;
        m_filterManaged = true;
        if (!effect.write(QVariant::fromValue(effectComponent))
            || !layerEnabled.write(true)) {
            qCWarning(lcLoomApply)
                << "Lo.style: could not take ownership of Item.layer.effect";
            release();
            m_filterFailed = true;
            return;
        }
    }
    m_filterComponent->setProperty("loomBlur", blur);
    m_filterComponent->setProperty("loomBrightness", brightness);
    m_filterComponent->setProperty("loomContrast", contrast);
    m_filterComponent->setProperty("loomSaturation", saturation);
    m_filterSignature = signature;
}

LoomStyleAttached *Lo::qmlAttachedProperties(QObject *object)
{
    return new LoomStyleAttached(object);
}
