#include "ItemFinder.h"
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <deque>
#include <unordered_set>

namespace chuck {

ItemFinder::ItemFinder(QQuickWindow& window)
    : m_window { &window }
{
}

QQuickItem* ItemFinder::contentRoot() const
{
    return m_window->contentItem();
}

QQuickItem* ItemFinder::overlayRoot() const
{
    if (m_cachedOverlay)
        return m_cachedOverlay;

    QQmlExpression expr(qmlContext(m_window), m_window, QStringLiteral("Overlay.overlay"));
    auto result = expr.evaluate();
    if (expr.hasError()) {
        qDebug() << "[chuck] Overlay expression error:" << expr.error().toString();
        return nullptr;
    }
    m_cachedOverlay = result.value<QQuickItem*>();
    return m_cachedOverlay;
}

// --- Identifier parsing ---

ItemFinder::ParsedIdentifier ItemFinder::parseIdentifier(const QString& identifier) const
{
    auto parsed = ParsedIdentifier {};

    static const auto delegateRx = QRegularExpression { QStringLiteral(R"(^(.+)\[(\d+)\]$)") };
    const auto match = delegateRx.match(identifier);
    if (match.hasMatch()) {
        parsed.pathSegments = match.captured(1).split('/');
        parsed.delegateIndex = match.captured(2).toInt();
        return parsed;
    }

    parsed.pathSegments = identifier.split('/');
    return parsed;
}

// --- Tree search ---

QQuickItem* ItemFinder::findByObjectName(QQuickItem* root, const QString& name) const
{
    if (!root)
        return nullptr;

    if (root->objectName() == name)
        return root;

    std::deque<QQuickItem*> queue;
    for (auto* child : effectiveChildren(root)) {
        queue.push_back(child);
    }

    while (!queue.empty()) {
        auto* item = queue.front();
        queue.pop_front();
        if (item->objectName() == name)
            return item;
        for (auto* child : effectiveChildren(item)) {
            queue.push_back(child);
        }
    }
    return nullptr;
}

std::expected<QQuickItem*, FindError> ItemFinder::findByNameOrType(QQuickItem* root, const QString& segment) const
{
    if (!root)
        return std::unexpected(FindError { QStringLiteral("item_not_found"), QStringLiteral("No root item") });

    QQuickItem* objectNameMatch = nullptr;
    QQuickItem* typeMatch = nullptr;
    int objectNameCount = 0;
    int typeCount = 0;

    if (root->objectName() == segment) {
        objectNameMatch = root;
        ++objectNameCount;
    } else if (itemTypeName(root) == segment) {
        typeMatch = root;
        ++typeCount;
    }

    std::deque<QQuickItem*> queue;
    for (auto* child : effectiveChildren(root)) {
        queue.push_back(child);
    }
    while (!queue.empty()) {
        auto* item = queue.front();
        queue.pop_front();
        if (item->objectName() == segment) {
            if (!objectNameMatch)
                objectNameMatch = item;
            ++objectNameCount;
            if (objectNameCount >= 2)
                break;
        } else if (itemTypeName(item) == segment) {
            if (!typeMatch)
                typeMatch = item;
            ++typeCount;
        }
        for (auto* child : effectiveChildren(item)) {
            queue.push_back(child);
        }
    }

    if (objectNameMatch) {
        if (objectNameCount > 1)
            return std::unexpected(FindError {
                QStringLiteral("ambiguous_item"),
                QStringLiteral("'%1' matches %2 items by objectName — use a path to disambiguate").arg(segment).arg(objectNameCount) });
        return objectNameMatch;
    }
    if (typeMatch) {
        if (typeCount > 1)
            return std::unexpected(FindError {
                QStringLiteral("ambiguous_item"),
                QStringLiteral("'%1' matches %2 items by type — use a more specific path").arg(segment).arg(typeCount) });
        return typeMatch;
    }
    return std::unexpected(FindError { QStringLiteral("item_not_found"), QStringLiteral("Segment not found: %1").arg(segment) });
}

std::expected<QQuickItem*, FindError> ItemFinder::findByPath(const QStringList& segments) const
{
    if (segments.isEmpty())
        return std::unexpected(FindError { QStringLiteral("item_not_found"), QStringLiteral("Empty path") });

    if (segments.size() == 1)
        return std::unexpected(FindError { QStringLiteral("item_not_found"), QStringLiteral("Single segment handled by findItem") });

    QQuickItem* current = nullptr;

    const auto& firstName = segments.first();
    auto contentResult = findByNameOrType(contentRoot(), firstName);
    auto overlayResult = findByNameOrType(overlayRoot(), firstName);

    if (contentResult)
        current = *contentResult;
    else if (overlayResult)
        current = *overlayResult;
    else {
        const auto& err = contentResult.error();
        return std::unexpected(err);
    }

    for (int i = 1; i < segments.size(); ++i) {
        auto childResult = findByNameOrType(current, segments[i]);
        if (!childResult)
            return std::unexpected(childResult.error());
        current = *childResult;
    }
    return current;
}

QQuickItem* ItemFinder::findDelegate(QQuickItem* parent, int index) const
{
    if (!parent) {
        qDebug() << "[chuck] findDelegate: null parent";
        return nullptr;
    }

    const auto contentItemVar = parent->property("contentItem");
    if (!contentItemVar.isValid()) {
        qDebug() << "[chuck] findDelegate: no contentItem on" << parent->objectName();
        return nullptr;
    }

    auto* contentItem = contentItemVar.value<QQuickItem*>();
    if (!contentItem) {
        qDebug() << "[chuck] findDelegate: contentItem is null on" << parent->objectName();
        return nullptr;
    }

    const auto children = contentItem->childItems();
    if (index < 0 || index >= children.size()) {
        qDebug() << "[chuck] findDelegate: index" << index << "out of range (0.." << children.size() - 1 << ") on" << parent->objectName();
        return nullptr;
    }

    return children[index];
}

// --- Public API ---

std::expected<FindResult, FindError> ItemFinder::findItem(const QString& identifier, QQuickItem* root) const
{
    if (identifier.isEmpty())
        return std::unexpected(FindError { QStringLiteral("item_not_found"), QStringLiteral("Empty identifier") });

    const auto parsed = parseIdentifier(identifier);

    if (parsed.delegateIndex >= 0) {
        const auto parentName = parsed.pathSegments.join('/');
        auto parentResult = findItem(parentName, root);
        if (!parentResult)
            return std::unexpected(parentResult.error());

        auto* delegate = findDelegate(parentResult->item, parsed.delegateIndex);
        if (!delegate)
            return std::unexpected(FindError { QStringLiteral("item_not_found"),
                QStringLiteral("Delegate [%1] not found in %2").arg(parsed.delegateIndex).arg(parentName) });

        return FindResult {
            delegate,
            parentResult->path + QStringLiteral("[%1]").arg(parsed.delegateIndex)
        };
    }

    if (parsed.pathSegments.size() > 1) {
        if (root) {
            auto first = findByNameOrType(root, parsed.pathSegments[0]);
            if (!first)
                return std::unexpected(first.error());
            auto* current = *first;
            for (int i = 1; i < parsed.pathSegments.size(); ++i) {
                auto child = findByNameOrType(current, parsed.pathSegments[i]);
                if (!child)
                    return std::unexpected(child.error());
                current = *child;
            }
            return FindResult { current, parsed.pathSegments.join('/') };
        }
        auto result = findByPath(parsed.pathSegments);
        if (result)
            return FindResult { *result, parsed.pathSegments.join('/') };
        return std::unexpected(result.error());
    }

    const auto& name = parsed.pathSegments.first();

    if (root) {
        auto result = findByNameOrType(root, name);
        if (!result)
            return std::unexpected(result.error());
        return FindResult { *result, name };
    }

    auto contentResult = findByNameOrType(contentRoot(), name);
    auto overlayResult = findByNameOrType(overlayRoot(), name);

    struct Match {
        QQuickItem* item;
        QString path;
    };
    std::vector<Match> candidates;

    if (contentResult)
        candidates.push_back({ *contentResult, name });
    if (overlayResult) {
        bool duplicate = contentResult && (*contentResult == *overlayResult);
        if (!duplicate)
            candidates.push_back({ *overlayResult, QStringLiteral("overlay/") + name });
    }

    if (candidates.size() > 1) {
        return std::unexpected(FindError {
            QStringLiteral("ambiguous_item"),
            QStringLiteral("'%1' found in both content and overlay tree — use 'overlay/%1' to disambiguate").arg(name) });
    }

    if (candidates.size() == 1)
        return FindResult { candidates.front().item, candidates.front().path };

    if (contentResult.error().code == QStringLiteral("ambiguous_item"))
        return std::unexpected(contentResult.error());
    if (overlayResult.error().code == QStringLiteral("ambiguous_item"))
        return std::unexpected(overlayResult.error());

    return std::unexpected(FindError { QStringLiteral("item_not_found"), QStringLiteral("Item not found: %1").arg(name) });
}

auto ItemFinder::clickPoint(QQuickItem& item) const -> std::optional<QPoint>
{
    if (!item.window()) {
        qWarning() << "[chuck] clickPoint: item not attached to window";
        return std::nullopt;
    }

    const auto center = QPointF(item.width() / 2.0, item.height() / 2.0);

    if (isInOverlay(item)) {
        if (auto* overlay = overlayRoot())
            return item.mapToItem(overlay, center).toPoint();
    }

    return item.mapToScene(center).toPoint();
}

bool ItemFinder::isInOverlay(QQuickItem& item) const
{
    auto* current = &item;
    while (current) {
        const auto className = QLatin1StringView(current->metaObject()->className());
        if (className == QLatin1StringView("QQuickPopupItem") || className == QLatin1StringView("QQuickDrawer"))
            return true;
        current = current->parentItem();
    }
    return false;
}

std::vector<FindResult> ItemFinder::findInteractiveItems(int maxDepth) const
{
    return findItems({}, true, false, maxDepth);
}

std::vector<FindResult> ItemFinder::findItems(const QString& type, bool interactiveOnly, bool accessibleOnly, int maxDepth, QQuickItem* customRoot) const
{
    std::vector<FindResult> results;

    if (accessibleOnly && !customRoot) {
        if (auto* modal = activeModalPopup()) {
            collectItems(modal, QStringLiteral("overlay"), type, interactiveOnly, accessibleOnly,
                maxDepth, 0, results);
            return results;
        }
    }

    if (customRoot) {
        const auto name = customRoot->objectName();
        const auto prefix = name.isEmpty() ? itemTypeName(customRoot) : name;
        collectItems(customRoot, prefix, type, interactiveOnly, accessibleOnly,
            maxDepth, 0, results);
        return results;
    }

    auto collect = [&](QQuickItem* root, const QString& prefix) {
        if (!root)
            return;
        collectItems(root, prefix, type, interactiveOnly, accessibleOnly,
            maxDepth, 0, results);
    };

    collect(contentRoot(), {});
    collect(overlayRoot(), QStringLiteral("overlay"));

    std::unordered_set<QQuickItem*> overlayItems;
    for (const auto& r : results) {
        if (r.path.startsWith(QStringLiteral("overlay/")))
            overlayItems.insert(r.item);
    }
    std::erase_if(results, [&](const FindResult& r) {
        return overlayItems.contains(r.item) && !r.path.startsWith(QStringLiteral("overlay/"));
    });

    return results;
}

void ItemFinder::collectItems(QQuickItem* root, const QString& currentPath, const QString& typeFilter, bool interactiveOnly, bool accessibleOnly, int maxDepth, int currentDepth, std::vector<FindResult>& results) const
{
    if (!root)
        return;
    if (maxDepth >= 0 && currentDepth > maxDepth)
        return;

    if (accessibleOnly && !isOnScreen(*root))
        return;

    const auto name = root->objectName();
    const auto typeName = itemTypeName(root);
    auto path = currentPath;
    if (!name.isEmpty())
        path = currentPath.isEmpty() ? name : currentPath + '/' + name;
    else if (!typeName.isEmpty())
        path = currentPath.isEmpty() ? typeName : currentPath + '/' + typeName;

    bool passesType = typeFilter.isEmpty()
        || QString::fromLatin1(root->metaObject()->className()).contains(typeFilter);
    bool passesInteractive = !interactiveOnly || isInteractive(root);
    bool passesAccessible = !accessibleOnly || (root->isVisible() && root->isEnabled());

    if (passesType && passesInteractive && passesAccessible)
        results.emplace_back(root, path);

    if (interactiveOnly && isInteractive(root))
        return;

    for (auto* child : effectiveChildren(root)) {
        collectItems(child, path, typeFilter, interactiveOnly, accessibleOnly,
            maxDepth, currentDepth + 1, results);
    }
}

QList<QQuickItem*> ItemFinder::effectiveChildren(QQuickItem* item) const
{
    if (!item)
        return {};

    const auto contentItemVar = item->property("contentItem");
    if (contentItemVar.isValid()) {
        if (auto* contentItem = contentItemVar.value<QQuickItem*>()) {
            if (contentItem != item && contentItem->parentItem() == item)
                return contentItem->childItems();
        }
    }

    return item->childItems();
}

bool ItemFinder::isInteractive(QQuickItem* item) const
{
    if (!item)
        return false;

    const auto className = QString::fromLatin1(item->metaObject()->className());

    if (className.contains(QStringLiteral("TextField"))
        || className.contains(QStringLiteral("TextArea"))
        || className.contains(QStringLiteral("ComboBox"))
        || className.contains(QStringLiteral("CheckBox"))
        || className.contains(QStringLiteral("Button"))
        || className.contains(QStringLiteral("RadioButton"))
        || className.contains(QStringLiteral("Switch"))
        || className.contains(QStringLiteral("Slider"))
        || className.contains(QStringLiteral("SpinBox"))
        || className.contains(QStringLiteral("TabButton"))
        || className.contains(QStringLiteral("MenuItem")))
        return true;

    if (className.contains(QStringLiteral("Label"))
        || className.contains(QStringLiteral("Overlay"))
        || className.contains(QStringLiteral("RootItem"))
        || className.contains(QStringLiteral("Card"))
        || className.contains(QStringLiteral("ScrollView"))
        || className.contains(QStringLiteral("Flickable"))
        || className.contains(QStringLiteral("ListView"))
        || className.contains(QStringLiteral("GridView")))
        return false;

    if (item->acceptedMouseButtons() != Qt::NoButton)
        return true;

    return false;
}

QQuickItem* ItemFinder::firstInteractiveDescendant(QQuickItem* item) const
{
    if (!item)
        return nullptr;

    std::deque<QQuickItem*> queue;
    for (auto* child : effectiveChildren(item))
        queue.push_back(child);

    while (!queue.empty()) {
        auto* candidate = queue.front();
        queue.pop_front();
        if (candidate->isVisible() && candidate->acceptedMouseButtons() != Qt::NoButton
            && isInteractive(candidate))
            return candidate;
        for (auto* child : effectiveChildren(candidate))
            queue.push_back(child);
    }
    return nullptr;
}

bool ItemFinder::isOnScreen(QQuickItem& item) const
{
    const auto sceneRect = item.mapRectToScene(QRectF { 0, 0, item.width(), item.height() });
    const auto windowRect = QRectF { 0, 0,
        static_cast<qreal>(m_window->width()), static_cast<qreal>(m_window->height()) };
    return sceneRect.intersects(windowRect);
}

QQuickItem* ItemFinder::activeModalPopup() const
{
    auto* overlay = overlayRoot();
    if (!overlay)
        return nullptr;

    for (auto* child : overlay->childItems()) {
        if (!child->isVisible())
            continue;
        const auto className = QString::fromLatin1(child->metaObject()->className());
        if (!className.contains(QStringLiteral("PopupItem")))
            continue;
        auto* popupObj = child->parent();
        if (popupObj && popupObj->property("modal").toBool())
            return child;
    }
    return nullptr;
}

bool ItemFinder::isInsideModalPopup(QQuickItem* item, QQuickItem* modalPopup) const
{
    for (auto* cur = item; cur; cur = cur->parentItem()) {
        if (cur == modalPopup)
            return true;
    }
    return false;
}

QString ItemFinder::itemTypeName(QQuickItem* item) const
{
    if (!item)
        return {};

    auto className = QString::fromLatin1(item->metaObject()->className());

    const auto idx = className.indexOf(QLatin1StringView("_QMLTYPE_"));
    if (idx > 0)
        return className.left(idx);

    if (className.startsWith(QLatin1StringView("QQuick")))
        return className.mid(6);

    return className;
}

} // namespace chuck
