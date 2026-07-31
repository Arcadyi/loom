#include "commands.h"

#include <QCoreApplication>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("loom"));
    QCoreApplication::setApplicationVersion(QStringLiteral(LOOM_VERSION_STR));

    Commands commands;
    return commands.execute(QCoreApplication::arguments().sliced(1));
}
