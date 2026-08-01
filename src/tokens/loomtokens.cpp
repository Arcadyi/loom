#include "loomtokens.h"

#include <QAccessibilityHints>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QStyleHints>
#include <QUrl>

#include "loomconfigloader.h"
#include "loomiconprovider.h"
#include "loomtokenregistry.h"
#include "style/loomstyleattached.h"

LoomTokens::LoomTokens(QObject *parent)
    : QObject(parent)
    , m_color(new LoomColorGroup(this))
    , m_space(new LoomSpaceGroup(this))
    , m_text(new LoomTextGroup(this))
    , m_font(new LoomFontGroup(this))
    , m_radius(new LoomRadiusGroup(this))
    , m_shadow(new LoomShadowGroup(this))
    , m_opacity(new LoomOpacityGroup(this))
    , m_duration(new LoomDurationGroup(this))
    , m_easing(new LoomEasingGroup(this))
    , m_breakpoint(new LoomBreakpointGroup(this))
{
    connect(
        LoomTokenRegistry::instance(), &LoomTokenRegistry::themeChanged, this,
        &LoomTokens::themeChanged);
    connect(
        LoomTokenRegistry::instance(), &LoomTokenRegistry::accessibilityChanged, this,
        &LoomTokens::accessibilityChanged);
    if (QGuiApplication::instance()) {
        if (QGuiApplication::styleHints()->accessibility()) {
            connect(
                QGuiApplication::styleHints()->accessibility(),
                &QAccessibilityHints::contrastPreferenceChanged, this,
                &LoomTokens::accessibilityChanged);
        }
    }
}

QString LoomTokens::version() const
{
    return QStringLiteral(LOOM_VERSION_STR);
}

LoomColorGroup *LoomTokens::color() const
{
    return m_color;
}

LoomSpaceGroup *LoomTokens::space() const
{
    return m_space;
}

LoomTextGroup *LoomTokens::text() const
{
    return m_text;
}

LoomFontGroup *LoomTokens::font() const
{
    return m_font;
}

LoomRadiusGroup *LoomTokens::radius() const
{
    return m_radius;
}

LoomShadowGroup *LoomTokens::shadow() const
{
    return m_shadow;
}

LoomOpacityGroup *LoomTokens::opacity() const
{
    return m_opacity;
}

LoomDurationGroup *LoomTokens::duration() const
{
    return m_duration;
}

LoomEasingGroup *LoomTokens::easing() const
{
    return m_easing;
}

LoomBreakpointGroup *LoomTokens::breakpoint() const
{
    return m_breakpoint;
}

QUrl LoomTokens::iconRoot() const
{
    return loomIconRoot();
}

void LoomTokens::setIconRoot(const QUrl &root)
{
    // Compared after the write, not before: the setter normalises a missing
    // trailing slash, so the stored form is the only one worth diffing.
    const QUrl previous = loomIconRoot();
    setLoomIconRoot(root);
    if (loomIconRoot() != previous)
        emit iconRootChanged();
}

QString LoomTokens::theme() const
{
    return LoomTokenRegistry::instance()->theme();
}

void LoomTokens::setTheme(const QString &name)
{
    LoomTokenRegistry::instance()->setTheme(name);
}

bool LoomTokens::isDark() const
{
    return LoomTokenRegistry::instance()->isDark();
}

LoomTokens::ThemeMode LoomTokens::themeMode() const
{
    return LoomTokenRegistry::instance()->themeMode()
            == LoomTokenRegistry::ThemeMode::System
        ? SystemTheme
        : ExplicitTheme;
}

void LoomTokens::setThemeMode(ThemeMode mode)
{
    LoomTokenRegistry::instance()->setThemeMode(
        mode == SystemTheme ? LoomTokenRegistry::ThemeMode::System
                            : LoomTokenRegistry::ThemeMode::Explicit);
}

bool LoomTokens::highContrast() const
{
    return QGuiApplication::instance() && QGuiApplication::styleHints()->accessibility()
        && QGuiApplication::styleHints()->accessibility()->contrastPreference()
        == Qt::ContrastPreference::HighContrast;
}

LoomTokens::MotionPreference LoomTokens::motionPreference() const
{
    switch (LoomTokenRegistry::instance()->motionMode()) {
    case LoomTokenRegistry::MotionMode::System:
        return SystemMotion;
    case LoomTokenRegistry::MotionMode::Reduce:
        return ReduceMotion;
    case LoomTokenRegistry::MotionMode::Full:
        return FullMotion;
    }
    return SystemMotion;
}

void LoomTokens::setMotionPreference(MotionPreference preference)
{
    const auto mode = preference == ReduceMotion ? LoomTokenRegistry::MotionMode::Reduce
        : preference == FullMotion               ? LoomTokenRegistry::MotionMode::Full
                                                 : LoomTokenRegistry::MotionMode::System;
    LoomTokenRegistry::instance()->setMotionMode(mode);
}

bool LoomTokens::reduceMotion() const
{
    return LoomTokenRegistry::instance()->reduceMotion();
}

QStringList LoomTokens::themes() const
{
    return LoomTokenRegistry::instance()->themeNames();
}

QVariantMap LoomTokens::inspect(QObject *object) const
{
    if (!object)
        return {};
    auto *attached =
        qobject_cast<LoomStyleAttached *>(qmlAttachedPropertiesObject<Lo>(object, false));
    return attached ? attached->debugInfo() : QVariantMap{};
}

bool LoomTokens::loadConfig(const QUrl &url)
{
    QString path;
    if (url.isLocalFile())
        path = url.toLocalFile();
    else if (url.scheme() == QLatin1String("qrc"))
        path = QLatin1Char(':') + url.path();
    else
        path = url.toString();
    return loomLoadConfigFile(path);
}

QUrl LoomTokens::icon(const QUrl &source, const QColor &color) const
{
    // Registered here rather than from a QML_SINGLETON create() factory:
    // QQmlPrivate::singletonConstructionMode() prefers a default constructor
    // over the factory, and LoomTokens has one, so a create() override would
    // be compiled in and never called. Minting a URL is also the only way one
    // can exist, so first use is exactly when the provider becomes reachable.
    if (QQmlEngine *engine = qmlEngine(this)) {
        if (!engine->imageProvider(loomIconProviderName()))
            engine->addImageProvider(loomIconProviderName(), new LoomIconProvider);
    }
    return loomIconUrl(loomResolveIconSource(source), color);
}
