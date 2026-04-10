#pragma once

#include "PendingWait.h"
#include <QJsonObject>
#include <QObject>
#include <memory>
#include <unordered_map>

class QQuickWindow;

namespace chuck {

class ItemFinder;

class CommandHandler : public QObject {
    Q_OBJECT

public:
    explicit CommandHandler(QQuickWindow& window, std::shared_ptr<ItemFinder> finder);
    auto handleCommand(const QJsonObject& request, const std::function<void(const QJsonObject&)>& respond) -> void;

private:
    auto cmdFind(const QJsonObject& params) -> QJsonObject;
    auto cmdGet(const QJsonObject& params) -> QJsonObject;
    auto cmdClick(const QJsonObject& params) -> QJsonObject;
    auto cmdKeys(const QJsonObject& params) -> QJsonObject;
    auto cmdWait(int id, const QJsonObject& params, const std::function<void(const QJsonObject&)>& respond) -> void;
    auto cmdQuit() -> QJsonObject;
    auto error(int id, const QString& code, const QString& message, const QJsonObject& extra = {}) -> QJsonObject;
    QQuickWindow* m_window; // non-owning, must outlive this instance
    std::shared_ptr<ItemFinder> m_finder;
    std::unordered_map<int, std::unique_ptr<PendingWait>> m_pendingWaits;
    int m_nextWaitId = 0;
};

} // namespace chuck
