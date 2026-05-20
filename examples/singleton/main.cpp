#include "AppState.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <tester/TesterServer.h>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    engine.loadFromModule("SingletonExample", "Main");

    if (engine.rootObjects().isEmpty())
        return -1;

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window)
        return -1;

    // Pass the engine explicitly so '@SingletonExample/AppState' identifiers resolve.
    // qmlEngine(window) would also work here since the window came from loadFromModule,
    // but being explicit makes the dependency obvious to a reader of this example.
    tester::TesterServer server(*window, { .port = 1408, .engine = &engine });
    server.start();

    return app.exec();
}
