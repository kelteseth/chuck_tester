#include "chuck/ChuckServer.h"
#include "CommandHandler.h"
#include "ItemFinder.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QWebSocket>
#include <QWebSocketServer>

namespace chuck {

ChuckServer::ChuckServer(QQuickWindow& window, Config config)
    : m_server { std::make_unique<QWebSocketServer>(QStringLiteral("Chuck Automation"), QWebSocketServer::NonSecureMode) }
    , m_handler { std::make_unique<CommandHandler>(window, std::make_shared<ItemFinder>(window)) }
    , m_port { config.port }
{
}

ChuckServer::ChuckServer(QQuickWindow& window)
    : ChuckServer { window, Config {} }
{
}

ChuckServer::~ChuckServer()
{
    stop();
}

auto ChuckServer::start() -> bool
{
    if (m_server->isListening())
        return true;

    if (!m_server->listen(QHostAddress::LocalHost, m_port)) {
        qCritical() << "[chuck] Failed to start on port" << m_port << m_server->errorString();
        return false;
    }

    connect(m_server.get(), &QWebSocketServer::newConnection, this, &ChuckServer::onNewConnection);

    qInfo() << "[chuck] Listening on port" << m_server->serverPort();
    return true;
}

auto ChuckServer::stop() -> void
{
    for (auto* client : m_clients) {
        disconnect(client, nullptr, this, nullptr);
        client->close();
        client->deleteLater();
    }
    m_clients.clear();
    m_server->close();
    qInfo() << "[chuck] Stopped";
}

auto ChuckServer::onNewConnection() -> void
{
    auto* socket = m_server->nextPendingConnection();
    if (!socket) {
        qWarning() << "[chuck] nextPendingConnection() returned null";
        return;
    }

    connect(socket, &QWebSocket::textMessageReceived, this, &ChuckServer::onTextMessageReceived);
    connect(socket, &QWebSocket::disconnected, this, &ChuckServer::onClientDisconnected);

    m_clients.insert(socket);
    qInfo() << "[chuck] Client connected (" << m_clients.size() << "total)";
}

auto ChuckServer::onTextMessageReceived(const QString& message) -> void
{
    auto* senderSocket = qobject_cast<QWebSocket*>(QObject::sender());
    if (!senderSocket) {
        qWarning() << "[chuck] textMessageReceived from null sender";
        return;
    }

    const auto doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) {
        const auto err = QJsonObject {
            { QStringLiteral("id"), -1 },
            { QStringLiteral("ok"), false },
            {
                QStringLiteral("error"),
                QJsonObject {
                    { QStringLiteral("code"), QStringLiteral("parse_error") },
                    { QStringLiteral("message"), QStringLiteral("Invalid JSON") },
                },
            },
        };
        sendTo(*senderSocket, QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact)));
        return;
    }

    const auto request = doc.object();
    qDebug() << "[chuck] Received:" << request["cmd"].toString() << "id:" << request["id"].toInt();

    auto safeSocket = QPointer<QWebSocket> { senderSocket };
    m_handler->handleCommand(request, [this, safeSocket](const QJsonObject& response) {
        if (!safeSocket) {
            qDebug() << "[chuck] Dropping response — client disconnected";
            return;
        }
        const auto json = QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact));
        sendTo(*safeSocket, json);
    });
}

auto ChuckServer::onClientDisconnected() -> void
{
    auto* socket = qobject_cast<QWebSocket*>(QObject::sender());
    if (!socket) {
        qWarning() << "[chuck] disconnected from null sender";
        return;
    }

    m_clients.erase(socket);
    socket->deleteLater();
    qInfo() << "[chuck] Client disconnected (" << m_clients.size() << "remaining)";
}

auto ChuckServer::sendTo(QWebSocket& client, const QString& message) -> void
{
    if (client.isValid())
        client.sendTextMessage(message);
}

} // namespace chuck
