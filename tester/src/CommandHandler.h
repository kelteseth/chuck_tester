#pragma once

#include "ItemFinder.h"
#include "PendingWait.h"
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <memory>
#include <unordered_map>

class QQuickWindow;

namespace tester {

// Bump when methods are added or their params/results change shape.
// 1: initial (no "version" method), 2: version, hover, scroll, window, set, invoke.
inline constexpr int ProtocolVersion = 2;

class CommandHandler : public QObject {
    Q_OBJECT

public:
    explicit CommandHandler(QQuickWindow& window, std::shared_ptr<ItemFinder> finder, bool allowScripting = true);
    auto handleCommand(const QJsonObject& request, const std::function<void(const QJsonObject&)>& respond) -> void;

private:
    auto cmdVersion() const -> QJsonObject;
    auto cmdFind(const QJsonObject& params) -> QJsonObject;
    auto cmdGet(const QJsonObject& params) -> QJsonObject;
    auto cmdClick(const QJsonObject& params) -> QJsonObject;
    auto cmdDrag(const QJsonObject& params) -> QJsonObject;
    auto cmdSwipe(const QJsonObject& params) -> QJsonObject;
    auto cmdHover(const QJsonObject& params) -> QJsonObject;
    auto cmdScroll(const QJsonObject& params) -> QJsonObject;
    auto cmdWindow(const QJsonObject& params) -> QJsonObject;
    auto cmdSet(const QJsonObject& params) -> QJsonObject;
    auto cmdInvoke(const QJsonObject& params) -> QJsonObject;
    auto cmdKeys(const QJsonObject& params) -> QJsonObject;
    auto cmdScreenshot(const QJsonObject& params) -> QJsonObject;
    auto cmdTree(const QJsonObject& params) -> QJsonObject;
    auto cmdWait(const QJsonValue& id, const QJsonObject& params, const std::function<void(const QJsonObject&)>& respond) -> void;
    auto cmdQuit() -> QJsonObject;

    auto windowInfo() const -> QJsonObject;
    auto resolveInteractive(const QJsonObject& params, const QString& key, const char* command) -> std::expected<QQuickItem*, QJsonObject>;
    auto resolveItem(const QJsonValue& itemParam, QQuickItem* root = nullptr) -> std::expected<FindResult, FindError>;
    static auto jsonRpcResult(const QJsonValue& id, const QJsonValue& result) -> QJsonObject;
    static auto jsonRpcError(const QJsonValue& id, int code, const QString& message, const QJsonObject& data = {}) -> QJsonObject;

    QQuickWindow* m_window;
    std::shared_ptr<ItemFinder> m_finder;
    std::unordered_map<int, std::unique_ptr<PendingWait>> m_pendingWaits;
    int m_nextWaitId = 0;
    bool m_allowScripting = true;
};

} // namespace tester
