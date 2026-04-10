#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <tester/TesterServer.h>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    engine.loadFromModule("MinimalExample", "Main");

    if (engine.rootObjects().isEmpty())
        return -1;

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window)
        return -1;

    // Start the tester automation server on the default port
    tester::TesterServer server(*window);
    server.start();

    return app.exec();
}
