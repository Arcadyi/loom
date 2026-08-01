#include "languageserverproxy.h"

#include <QCoreApplication>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("qmlls"));
    QCoreApplication::setApplicationVersion(QStringLiteral(LOOM_VERSION_STR));

    lsp::LanguageServerProxy proxy;
    return proxy.run({}, QCoreApplication::arguments().sliced(1));
}
