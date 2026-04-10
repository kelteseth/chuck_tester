#pragma once

#include <QJsonObject>
#include <QMetaObject>
#include <QObject>
#include <QTimer>
#include <QVariant>
#include <functional>
#include <memory>
#include <vector>

class QQuickItem;

namespace chuck {

class ItemFinder;

class PendingWait : public QObject {
    Q_OBJECT

public:
    using Callback = std::function<void(const QJsonObject& response)>;
    explicit PendingWait(int requestId, Callback callback);
    ~PendingWait() override;
    int requestId() const { return m_requestId; }
    auto waitForProperty(QObject* target, const QString& property, const QVariant& expectedValue, int timeoutMs) -> void;
    auto waitForItem(std::shared_ptr<ItemFinder> finder, const QString& identifier, const QString& property, const QVariant& expectedValue, int timeoutMs, QQuickItem* contentRoot, QQuickItem* overlayRoot) -> void;

signals:
    void finished(int requestId);

private:
    auto resolve(const QJsonObject& result) -> void;
    auto reject(int code, const QString& message) -> void;
    auto cleanup() -> void;
    auto connectPropertyNotify(QObject* target, const QString& property, const QVariant& expectedValue) -> void;
    auto checkPropertyNow(QObject* target, const QString& property, const QVariant& expectedValue) -> void;
    Q_INVOKABLE void checkStoredProperty();
    auto onTargetDestroyed() -> void;
    int m_requestId = 0;
    Callback m_callback;
    bool m_resolved = false;
    QObject* m_notifyTarget = nullptr;
    QString m_notifyProperty;
    QVariant m_notifyExpectedValue;
    QTimer m_timeout;
    std::vector<QMetaObject::Connection> m_connections;
    std::vector<QTimer*> m_pollTimers;
};

} // namespace chuck
