#include "loomtokens.h"

#include <QQmlEngine>
#include <QUrl>

#include "loomconfigloader.h"
#include "loomiconprovider.h"
#include "loomtokenregistry.h"

LoomTokens::LoomTokens(QObject *parent)
    : QObject(parent)
    , m_color(new LoomColorGroup(this))
    , m_space(new LoomSpaceGroup(this))
    , m_text(new LoomTextGroup(this))
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

QStringList LoomTokens::themes() const
{
    return LoomTokenRegistry::instance()->themeNames();
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
