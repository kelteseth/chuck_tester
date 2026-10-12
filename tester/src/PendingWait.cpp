#include "PendingWait.h"
#include "ItemFinder.h"
#include <QJsonValue>
#include <QMetaProperty>
#include <QQuickItem>

namespace tester {

PendingWait::PendingWait(int requestId, Callback callback)
    : m_requestId { requestId }
    , m_callback { std::move(callback) }
{
    m_timeout.setSingleShot(true);
}

PendingWait::~PendingWait()
{
    cleanup();
}

auto PendingWait::waitForProperty(QObject* target, const QString& property, const QVariant& expectedValue, int timeoutMs) -> void
{
    checkPropertyNow(target, property, expectedValue);
    if (m_resolved)
        return;

    m_connections.push_back(connect(target, &QObject::destroyed, this, &PendingWait::onTargetDestroyed));

    connectPropertyNotify(target, property, expectedValue);

    connect(&m_timeout, &QTimer::timeout, this, [this, property]() {
        reject(ErrTimeout, QStringLiteral("Timeout waiting for property '%1'").arg(property));
    });
    m_timeout.start(timeoutMs);
}

auto PendingWait::waitForItem(std::shared_ptr<ItemFinder> finder, const QString& identifier, const QString& property, const QVariant& expectedValue, int timeoutMs, QQuickItem* contentRoot, QQuickItem* overlayRoot) -> void
{
    auto retryFind = [this, finder, identifier, property, expectedValue]() {
        auto match = finder->findItem(identifier);
        if (!match)
            return;

        qDebug() << "[tester] Item appeared:" << identifier;

        if (property.isEmpty()) {
            resolve(QJsonObject { { QStringLiteral("found"), identifier } });
            return;
        }

        cleanup();

        checkPropertyNow(match->object, property, expectedValue);
        if (m_resolved)
            return;

        m_connections.push_back(connect(match->object, &QObject::destroyed, this, &PendingWait::onTargetDestroyed));

        connectPropertyNotify(match->object, property, expectedValue);
    };

    if (contentRoot) {
        m_connections.push_back(connect(contentRoot, &QQuickItem::childrenChanged, this, retryFind));
    }
    if (overlayRoot) {
        m_connections.push_back(connect(overlayRoot, &QQuickItem::childrenChanged, this, retryFind));
    }

    auto* pollTimer = new QTimer(this);
    pollTimer->setInterval(200);
    connect(pollTimer, &QTimer::timeout, this, retryFind);
    pollTimer->start();
    m_pollTimers.push_back(pollTimer);

    connect(&m_timeout, &QTimer::timeout, this, [this, identifier]() {
        reject(ErrTimeout, QStringLiteral("Timeout waiting for item '%1'").arg(identifier));
    });
    m_timeout.start(timeoutMs);
}

auto PendingWait::connectPropertyNotify(QObject* target, const QString& property, const QVariant& expectedValue) -> void
{
    const auto* meta = target->metaObject();
    const auto propIndex = meta->indexOfProperty(property.toLatin1().constData());
    if (propIndex < 0) {
        reject(ErrPropertyNotFound, QStringLiteral("Property '%1' not found").arg(property));
        return;
    }

    const auto metaProp = meta->property(propIndex);
    if (!metaProp.hasNotifySignal()) {
        qDebug() << "[tester] Property" << property << "has no NOTIFY signal, using poll fallback";
        auto* pollTimer = new QTimer(this);
        pollTimer->setInterval(100);
        connect(pollTimer, &QTimer::timeout, this, [this, target, property, expectedValue]() {
            checkPropertyNow(target, property, expectedValue);
        });
        pollTimer->start();
        m_pollTimers.push_back(pollTimer);
        return;
    }

    m_notifyTarget = target;
    m_notifyProperty = property;
    m_notifyExpectedValue = expectedValue;

    const auto signalIndex = metaProp.notifySignalIndex();
    const auto slotIndex = metaObject()->indexOfMethod("checkStoredProperty()");
    QMetaObject::connect(target, signalIndex, this, slotIndex);
}

void PendingWait::checkStoredProperty()
{
    if (m_notifyTarget)
        checkPropertyNow(m_notifyTarget, m_notifyProperty, m_notifyExpectedValue);
}

auto PendingWait::checkPropertyNow(QObject* target, const QString& property, const QVariant& expectedValue) -> void
{
    if (m_resolved)
        return;

    const auto current = target->property(property.toLatin1().constData());
    // The expected value arrives as JSON, so a type with no QVariant
    // conversion from it (QColor, QUrl, ...) never compares equal directly.
    // Fall back to the JSON form 'get' reports, so wait accepts what get shows.
    if (current == expectedValue || QJsonValue::fromVariant(current) == QJsonValue::fromVariant(expectedValue)) {
        resolve(QJsonObject {
            { QStringLiteral("property"), property },
            { QStringLiteral("value"), QJsonValue::fromVariant(current) },
        });
    }
}

auto PendingWait::onTargetDestroyed() -> void
{
    reject(ErrItemNotFound, QStringLiteral("Target item was destroyed"));
}

auto PendingWait::resolve(const QJsonObject& result) -> void
{
    if (m_resolved)
        return;
    m_resolved = true;
    cleanup();

    const auto response = QJsonObject {
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("result"), result },
    };
    m_callback(response);
    emit finished(m_requestId);
}

auto PendingWait::reject(int code, const QString& message) -> void
{
    if (m_resolved)
        return;
    m_resolved = true;
    cleanup();

    const auto response = QJsonObject {
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        {
            QStringLiteral("error"),
            QJsonObject {
                { QStringLiteral("code"), code },
                { QStringLiteral("message"), message },
            },
        },
    };
    m_callback(response);
    emit finished(m_requestId);
}

auto PendingWait::cleanup() -> void
{
    m_timeout.stop();
    for (auto* t : m_pollTimers)
        t->stop();
    m_pollTimers.clear();
    for (const auto& conn : m_connections) {
        QObject::disconnect(conn);
    }
    m_connections.clear();
}

} // namespace tester
