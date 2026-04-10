#pragma once

#include <QObject>
#include <QPointer>
#include <memory>
#include <unordered_set>

class QWebSocket;
class QWebSocketServer;
class QQuickWindow;

namespace tester {

class CommandHandler;

class TesterServer : public QObject {
    Q_OBJECT

public:
    struct Config {
        quint16 port = 1408;
    };
    explicit TesterServer(QQuickWindow& window, Config config);
    explicit TesterServer(QQuickWindow& window);
    ~TesterServer() override;
    auto start() -> bool;
    auto stop() -> void;

private:
    auto onNewConnection() -> void;
    auto onTextMessageReceived(const QString& message) -> void;
    auto onClientDisconnected() -> void;
    auto sendTo(QWebSocket& client, const QString& message) -> void;

private:
    std::unique_ptr<QWebSocketServer> m_server;
    std::unique_ptr<CommandHandler> m_handler;
    std::unordered_set<QWebSocket*> m_clients;
    quint16 m_port = 0;
};

} // namespace tester
