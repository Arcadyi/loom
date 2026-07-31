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

bool loadConfig(const QString &filePath)
{
    return loomLoadConfigFile(filePath);
}

bool reloadConfig(const QString &filePath)
{
    return loomReloadConfigFile(filePath);
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
