#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <loom/loom.h>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    if (!loom::loadConfig(QStringLiteral(":/gallery/design/design/tokens.json")))
        return 1;

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("LoomGallery", "Main");

    return app.exec();
}
