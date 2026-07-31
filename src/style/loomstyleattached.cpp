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
    qCWarning(lcLoomApply).noquote()
        << "Lo.style: utility" << key << "is not supported on" << type->className()
        << "- see docs/limitations.md";
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
    // Margins resolve against the parent's type (Layout vs. anchors), so a
    // reparent re-routes them.
    if (m_compiled && m_compiled->usesMargins)
        connect(
            m_target, &QQuickItem::parentChanged, this, &LoomStyleAttached::scheduleApply,
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
        if (width >= registry->breakpoint(QLatin1String(names[i])))
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
        quint8 variantCount;
    };
    QHash<QString, Desired> desired;
    QString shadowKey;
    quint8 shadowVariantCount = 0;
    // Tailwind defaults: 150ms, cubic-bezier(0.4, 0, 0.2, 1).
    struct {
        LoomTransitionMode mode = LoomTransitionMode::None;
        QString durationKey = QStringLiteral("150");
        QString easeKey = QStringLiteral("in-out");
        quint8 modeVariantCount = 0;
        quint8 durationVariantCount = 0;
        quint8 easeVariantCount = 0;
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
                if (rule.variantCount >= shadowVariantCount) {
                    shadowKey = rule.key;
                    shadowVariantCount = rule.variantCount;
                }
                continue;
            }
            if (rule.utility == LoomUtility::TransitionMode) {
                if (rule.variantCount >= transition.modeVariantCount) {
                    transition.mode = LoomTransitionMode(quint8(rule.literal));
                    transition.modeVariantCount = rule.variantCount;
                }
                continue;
            }
            if (rule.utility == LoomUtility::TransitionDuration) {
                if (rule.variantCount >= transition.durationVariantCount) {
                    transition.durationKey = rule.key;
                    transition.durationVariantCount = rule.variantCount;
                }
                continue;
            }
            if (rule.utility == LoomUtility::TransitionEase) {
                if (rule.variantCount >= transition.easeVariantCount) {
                    transition.easeKey = rule.key;
                    transition.easeVariantCount = rule.variantCount;
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
                writes.append({path, withAlpha(registry->color(rule.key), rule.alphaPercent)});
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
            case LoomUtility::Tracking: {
                // Em-relative: resolved against the size this same apply pass
                // is setting, falling back to the item's current size.
                qreal pixelSize = QQmlProperty(m_target, QStringLiteral("font.pixelSize"))
                                      .read()
                                      .toReal();
                if (const auto sized =
                        desired.constFind(QStringLiteral("font.pixelSize"));
                    sized != desired.constEnd())
                    pixelSize = sized->value.toReal();
                writes.append({path, registry->tracking(rule.key) * pixelSize});
                break;
            }
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
            case LoomUtility::Shadow:
            case LoomUtility::TransitionMode:
            case LoomUtility::TransitionDuration:
            case LoomUtility::TransitionEase:
                // Handled before the switch; not property writes.
                break;
            }

            for (const ResolvedWrite &write : writes) {
                // Specificity: a more-variant-qualified rule beats a plainer
                // one; at equal counts the later rule wins (iteration order).
                const auto existing = desired.constFind(write.path);
                if (existing != desired.constEnd()
                    && existing->variantCount > rule.variantCount)
                    continue;
                desired.insert(write.path, {write.value, rule.variantCount});
            }
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
        QQmlProperty(m_target, it.key(), context).write(m_originals.take(it.key()));
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
