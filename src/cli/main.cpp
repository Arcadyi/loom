#include "commands.h"

#include <QCoreApplication>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("respin"));
    QCoreApplication::setApplicationVersion(QStringLiteral(RESPIN_VERSION));

    Commands commands;
    return commands.execute(QCoreApplication::arguments().sliced(1));
}
