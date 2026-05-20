#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <tester/TesterServer.h>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    engine.loadFromModule("PopupExample", "Main");

    if (engine.rootObjects().isEmpty())
        return -1;

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window)
        return -1;

    tester::TesterServer server(*window, { .port = 1408, .engine = &engine });
    server.start();

    return app.exec();
}
