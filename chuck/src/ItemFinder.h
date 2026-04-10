#pragma once

#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <expected>
#include <optional>
#include <vector>

class QQuickItem;
class QQuickWindow;

namespace chuck {

struct FindResult {
    QQuickItem* item = nullptr;
    QString path;
};

struct FindError {
    QString code;
    QString message;
};

class ItemFinder : public QObject {
    Q_OBJECT

public:
    explicit ItemFinder(QQuickWindow& window);
    auto findItem(const QString& identifier, QQuickItem* root = nullptr) const -> std::expected<FindResult, FindError>;
    auto findInteractiveItems(int maxDepth = -1) const -> std::vector<FindResult>;
    auto findItems(const QString& type = {}, bool interactiveOnly = false, bool accessibleOnly = false, int maxDepth = -1, QQuickItem* customRoot = nullptr) const -> std::vector<FindResult>;
    auto clickPoint(QQuickItem& item) const -> std::optional<QPoint>;
    auto isInOverlay(QQuickItem& item) const -> bool;
    auto isOnScreen(QQuickItem& item) const -> bool;
    auto activeModalPopup() const -> QQuickItem*;
    auto isInsideModalPopup(QQuickItem* item, QQuickItem* modalPopup) const -> bool;
    auto contentRoot() const -> QQuickItem*;
    auto overlayRoot() const -> QQuickItem*;
    auto firstInteractiveDescendant(QQuickItem* item) const -> QQuickItem*;

private:
    struct ParsedIdentifier {
        QStringList pathSegments;
        int delegateIndex = -1;
    };
    auto parseIdentifier(const QString& identifier) const -> ParsedIdentifier;
    auto findByObjectName(QQuickItem* root, const QString& name) const -> QQuickItem*;
    auto findByNameOrType(QQuickItem* root, const QString& segment) const -> std::expected<QQuickItem*, FindError>;
    auto findByPath(const QStringList& segments) const -> std::expected<QQuickItem*, FindError>;
    auto findDelegate(QQuickItem* parent, int index) const -> QQuickItem*;
    auto collectItems(QQuickItem* root, const QString& currentPath, const QString& typeFilter, bool interactiveOnly, bool accessibleOnly, int maxDepth, int currentDepth, std::vector<FindResult>& results) const -> void;
    auto effectiveChildren(QQuickItem* item) const -> QList<QQuickItem*>;
    auto isInteractive(QQuickItem* item) const -> bool;
    auto itemTypeName(QQuickItem* item) const -> QString;
    QQuickWindow* m_window; // non-owning, must outlive this instance
    mutable QPointer<QQuickItem> m_cachedOverlay;
};

} // namespace chuck
