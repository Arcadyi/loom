#include <loom/loom.h>

#include "loomconfigloader.h"
#include "style/loomiconprovider.h"
#include "tokens/loomtokenregistry.h"

namespace loom {

const char *version()
{
    return LOOM_VERSION_STR;
}

void setTheme(const QString &name)
{
    LoomTokenRegistry::instance()->setTheme(name);
}

QString theme()
{
    return LoomTokenRegistry::instance()->theme();
}

void setThemeMode(ThemeMode mode)
{
    LoomTokenRegistry::instance()->setThemeMode(
        mode == ThemeMode::System ? LoomTokenRegistry::ThemeMode::System
                                  : LoomTokenRegistry::ThemeMode::Explicit);
}

ThemeMode themeMode()
{
    return LoomTokenRegistry::instance()->themeMode()
            == LoomTokenRegistry::ThemeMode::System
        ? ThemeMode::System
        : ThemeMode::Explicit;
}

bool loadConfig(const QString &filePath)
{
    return loomLoadConfigFile(filePath);
}

bool reloadConfig(const QString &filePath)
{
    return loomReloadConfigFile(filePath);
}

bool reloadConfigData(const QByteArray &json, const QString &basePath)
{
    return loomReloadConfigData(json, basePath);
}

void setIconRoot(const QUrl &root)
{
    setLoomIconRoot(root);
}

QUrl iconRoot()
{
    return loomIconRoot();
}

} // namespace loom
