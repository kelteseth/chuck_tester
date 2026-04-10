#include "CommandHandler.h"
#include "ItemFinder.h"
#include "PendingWait.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaProperty>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <cstdint>
#include <map>

namespace {

enum class PropsMode : std::uint8_t { All,
    Custom,
    Named,
    Minimal };

QJsonObject collectProperties(QQuickItem* item, PropsMode mode, const QStringList& names = {})
{
    if (mode == PropsMode::Minimal)
        return {};

    QJsonObject props;
    const auto* meta = item->metaObject();

    if (mode == PropsMode::Named) {
        for (const auto& name : names) {
            const auto value = item->property(name.toLatin1().constData());
            if (!value.isValid())
                continue;
            auto jv = QJsonValue::fromVariant(value);
            if (!jv.isUndefined())
                props[name] = jv;
        }
        return props;
    }

    const int startIdx = (mode == PropsMode::Custom)
        ? QQuickItem::staticMetaObject.propertyCount()
        : 0;

    for (int i = startIdx; i < meta->propertyCount(); ++i) {
        const auto prop = meta->property(i);
        auto jv = QJsonValue::fromVariant(prop.read(item));
        if (!jv.isUndefined())
            props[QString::fromLatin1(prop.name())] = jv;
    }
    return props;
}

struct PropsFilter {
    PropsMode mode = PropsMode::All;
    QStringList names;
    QString error;
    QString matchedKeyword;
};

PropsFilter parsePropsParam(const QJsonObject& params)
{
    if (!params.contains(QStringLiteral("props")))
        return {};
    const auto str = params[QStringLiteral("props")].toString();
    const auto upper = str.toUpper();
    if (upper == QStringLiteral("CUSTOM"))
        return { .mode = PropsMode::Custom, .matchedKeyword = QStringLiteral("custom") };
    if (upper == QStringLiteral("MINIMAL"))
        return { .mode = PropsMode::Minimal, .matchedKeyword = QStringLiteral("minimal") };
    if (upper == QStringLiteral("ALL"))
        return { .matchedKeyword = QStringLiteral("all") };

    return { .mode = PropsMode::Named, .names = str.split(',', Qt::SkipEmptyParts) };
}

QString checkPropsConflict(QQuickItem* item, const PropsFilter& filter)
{
    if (filter.matchedKeyword.isEmpty())
        return {};
    if (item->metaObject()->indexOfProperty(filter.matchedKeyword.toLatin1().constData()) >= 0)
        return QStringLiteral("Item has property '%1' which conflicts with reserved mode keyword '%2'")
            .arg(filter.matchedKeyword, filter.matchedKeyword.toUpper());
    return {};
}

QJsonObject itemToJson(QQuickItem* item, const QString& path, const PropsFilter& filter)
{
    QJsonObject obj {
        { QStringLiteral("objectName"), item->objectName() },
        { QStringLiteral("type"), QString::fromLatin1(item->metaObject()->className()) },
        { QStringLiteral("path"), path },
    };

    if (filter.mode == PropsMode::Minimal) {
        const auto mandatory = item->property("isMandatory");
        if (mandatory.isValid() && mandatory.toBool())
            obj[QStringLiteral("isMandatory")] = true;
        return obj;
    }

    auto props = collectProperties(item, filter.mode, filter.names);
    for (auto it = props.begin(); it != props.end(); ++it) {
        obj[it.key()] = it.value();
    }
    return obj;
}

QQuickItem* topmostMouseItemAt(QQuickItem* root, const QPointF& scenePos)
{
    if (!root->isVisible() || !root->isEnabled())
        return nullptr;

    const auto localPos = root->mapFromScene(scenePos);
    if (!root->contains(localPos))
        return nullptr;

    const auto children = root->childItems();
    for (int i = children.size() - 1; i >= 0; --i) {
        if (auto* hit = topmostMouseItemAt(children[i], scenePos))
            return hit;
    }

    if (root->acceptedMouseButtons() != Qt::NoButton)
        return root;
    return nullptr;
}

// Internal error sentinel: command methods return this on failure.
// handleCommand checks for "_err_code" to distinguish error from success result.
QJsonObject makeErr(int code, const QString& message, const QJsonObject& data = {})
{
    QJsonObject obj {
        { QStringLiteral("_err_code"), code },
        { QStringLiteral("_err_message"), message },
    };
    if (!data.isEmpty())
        obj[QStringLiteral("_err_data")] = data;
    return obj;
}

bool isErr(const QJsonObject& obj)
{
    return obj.contains(QStringLiteral("_err_code"));
}

} // namespace

namespace chuck {

namespace {

    QJsonObject checkAccessible(QQuickItem* item, const QString& identifier, ItemFinder& finder)
    {
        if (!item->isVisible())
            return makeErr(ErrItemNotVisible, QStringLiteral("Item not visible: %1").arg(identifier));
        if (!item->isEnabled())
            return makeErr(ErrItemDisabled, QStringLiteral("Item disabled: %1").arg(identifier));
        if (!item->window())
            return makeErr(ErrItemNotFound, QStringLiteral("Item not attached to window: %1").arg(identifier));
        if (!finder.isOnScreen(*item))
            return makeErr(ErrItemNotVisible, QStringLiteral("Item not on screen: %1").arg(identifier));
        if (auto* modal = finder.activeModalPopup()) {
            if (!finder.isInsideModalPopup(item, modal))
                return makeErr(ErrItemBlockedByModal, QStringLiteral("Item blocked by modal popup: %1").arg(identifier));
        }
        return {};
    }

} // namespace

CommandHandler::CommandHandler(QQuickWindow& window, std::shared_ptr<ItemFinder> finder)
    : m_window { &window }
    , m_finder { std::move(finder) }
{
}

// --- JSON-RPC 2.0 helpers ---

QJsonObject CommandHandler::jsonRpcResult(const QJsonValue& id, const QJsonValue& result)
{
    return {
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("id"), id },
        { QStringLiteral("result"), result },
    };
}

QJsonObject CommandHandler::jsonRpcError(const QJsonValue& id, int code, const QString& message, const QJsonObject& data)
{
    auto err = QJsonObject {
        { QStringLiteral("code"), code },
        { QStringLiteral("message"), message },
    };
    if (!data.isEmpty())
        err[QStringLiteral("data")] = data;

    return {
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("id"), id },
        { QStringLiteral("error"), err },
    };
}

// --- Item resolution (hybrid: string or property query object) ---

std::expected<FindResult, FindError> CommandHandler::resolveItem(const QJsonValue& itemParam, QQuickItem* root)
{
    if (itemParam.isString())
        return m_finder->findItem(itemParam.toString(), root);
    if (itemParam.isObject())
        return m_finder->findByProperties(itemParam.toObject(), root);
    return std::unexpected(FindError { ErrInvalidParams, QStringLiteral("'item' must be a string or object") });
}

// --- Dispatch ---

void CommandHandler::handleCommand(const QJsonObject& request, const std::function<void(const QJsonObject&)>& respond)
{
    const auto id = request[QStringLiteral("id")];
    const auto method = request[QStringLiteral("method")].toString();
    const auto params = request[QStringLiteral("params")].toObject();

    if (method.isEmpty()) {
        respond(jsonRpcError(id, ErrInvalidRequest, QStringLiteral("Missing 'method' field")));
        return;
    }

    QJsonObject result;

    if (method == QStringLiteral("find"))
        result = cmdFind(params);
    else if (method == QStringLiteral("get"))
        result = cmdGet(params);
    else if (method == QStringLiteral("click"))
        result = cmdClick(params);
    else if (method == QStringLiteral("drag"))
        result = cmdDrag(params);
    else if (method == QStringLiteral("swipe"))
        result = cmdSwipe(params);
    else if (method == QStringLiteral("keys"))
        result = cmdKeys(params);
    else if (method == QStringLiteral("screenshot"))
        result = cmdScreenshot(params);
    else if (method == QStringLiteral("tree"))
        result = cmdTree(params);
    else if (method == QStringLiteral("wait")) {
        cmdWait(id, params, respond);
        return;
    } else if (method == QStringLiteral("quit"))
        result = cmdQuit();
    else {
        respond(jsonRpcError(id, ErrMethodNotFound, QStringLiteral("Unknown method: %1").arg(method)));
        return;
    }

    if (isErr(result)) {
        respond(jsonRpcError(id, result[QStringLiteral("_err_code")].toInt(),
            result[QStringLiteral("_err_message")].toString(),
            result[QStringLiteral("_err_data")].toObject()));
    } else {
        respond(jsonRpcResult(id, result));
    }
}

// --- find ---

QJsonObject CommandHandler::cmdFind(const QJsonObject& params)
{
    const auto typeFilter = params[QStringLiteral("type")].toString();
    const auto interactiveOnly = params[QStringLiteral("interactive")].toBool(false);
    const auto accessibleOnly = params[QStringLiteral("accessible")].toBool(false);
    const auto maxDepth = params[QStringLiteral("depth")].toInt(-1);

    const auto filter = parsePropsParam(params);
    if (!filter.error.isEmpty())
        return makeErr(ErrInvalidParams, filter.error);

    QQuickItem* customRoot = nullptr;
    if (params.contains(QStringLiteral("root"))) {
        auto rootMatch = resolveItem(params[QStringLiteral("root")]);
        if (!rootMatch)
            return makeErr(rootMatch.error().code, rootMatch.error().message);
        customRoot = rootMatch->item;
    }

    auto items = m_finder->findItems(typeFilter, interactiveOnly, accessibleOnly, maxDepth, customRoot);

    if (filter.mode == PropsMode::Named && !items.empty()) {
        QStringList neverFound;
        for (const auto& name : filter.names) {
            const auto nameUtf8 = name.toLatin1();
            const auto found = std::ranges::any_of(items, [&](const auto& fr) {
                return fr.item->metaObject()->indexOfProperty(nameUtf8.constData()) >= 0;
            });
            if (!found)
                neverFound.append(name);
        }
        if (!neverFound.isEmpty())
            return makeErr(ErrPropertyNotFound,
                QStringLiteral("No items have properties: %1").arg(neverFound.join(QStringLiteral(", "))));
    }

    QJsonArray arr;
    for (const auto& fr : items) {
        const auto conflict = checkPropsConflict(fr.item, filter);
        if (!conflict.isEmpty())
            return makeErr(ErrInvalidParams, conflict);
        arr.append(itemToJson(fr.item, fr.path, filter));
    }
    return QJsonObject { { QStringLiteral("items"), arr } };
}

// --- get ---

QJsonObject CommandHandler::cmdGet(const QJsonObject& params)
{
    if (!params.contains(QStringLiteral("item")))
        return makeErr(ErrInvalidParams, QStringLiteral("Missing 'item' parameter"));

    auto match = resolveItem(params[QStringLiteral("item")]);
    if (!match)
        return makeErr(match.error().code, match.error().message);

    const auto filter = parsePropsParam(params);
    if (!filter.error.isEmpty())
        return makeErr(ErrInvalidParams, filter.error);
    const auto conflict = checkPropsConflict(match->item, filter);
    if (!conflict.isEmpty())
        return makeErr(ErrInvalidParams, conflict);
    if (filter.mode == PropsMode::Named) {
        const auto* meta = match->item->metaObject();
        QStringList unknown;
        for (const auto& name : filter.names) {
            if (meta->indexOfProperty(name.toLatin1().constData()) < 0)
                unknown.append(name);
        }
        if (!unknown.isEmpty())
            return makeErr(ErrPropertyNotFound,
                QStringLiteral("Unknown properties: %1").arg(unknown.join(QStringLiteral(", "))));
    }

    return itemToJson(match->item, match->path, filter);
}

// --- click ---

QJsonObject CommandHandler::cmdClick(const QJsonObject& params)
{
    if (!params.contains(QStringLiteral("item")))
        return makeErr(ErrInvalidParams, QStringLiteral("Missing 'item' parameter"));

    QQuickItem* customRoot = nullptr;
    if (params.contains(QStringLiteral("root"))) {
        auto rootMatch = resolveItem(params[QStringLiteral("root")]);
        if (!rootMatch)
            return makeErr(rootMatch.error().code, rootMatch.error().message);
        customRoot = rootMatch->item;
    }

    auto match = resolveItem(params[QStringLiteral("item")], customRoot);
    if (!match)
        return makeErr(match.error().code, match.error().message);

    const auto identifier = params[QStringLiteral("item")].isString()
        ? params[QStringLiteral("item")].toString()
        : QStringLiteral("<property query>");
    auto errObj = checkAccessible(match->item, identifier, *m_finder);
    if (!errObj.isEmpty())
        return errObj;

    auto* clickTarget = match->item;
    if (clickTarget->acceptedMouseButtons() == Qt::NoButton) {
        if (auto* child = m_finder->firstInteractiveDescendant(clickTarget))
            clickTarget = child;
    }

    auto point = m_finder->clickPoint(*clickTarget);
    if (!point)
        return makeErr(ErrClickFailed, QStringLiteral("Cannot get click point for item"));
    const auto clickedName = match->item->objectName();

    auto button = Qt::LeftButton;
    const auto buttonStr = params[QStringLiteral("button")].toString();
    if (buttonStr == QStringLiteral("right"))
        button = Qt::RightButton;
    else if (buttonStr == QStringLiteral("middle"))
        button = Qt::MiddleButton;

    // Hold (long press)
    const auto holdMs = params[QStringLiteral("hold")].toInt(0);
    if (holdMs > 0) {
        QTest::mousePress(m_window, button, Qt::NoModifier, *point);
        QTimer::singleShot(holdMs, this, [this, button, pt = *point]() {
            QTest::mouseRelease(m_window, button, Qt::NoModifier, pt);
        });
        return QJsonObject {
            { QStringLiteral("clicked"), clickedName },
            { QStringLiteral("hold"), holdMs },
            { QStringLiteral("point"), QJsonArray { point->x(), point->y() } },
        };
    }

    auto* topmost = topmostMouseItemAt(m_window->contentItem(), *point);
    bool targetReachable = false;
    for (auto* cur = topmost; cur; cur = cur->parentItem()) {
        if (cur == clickTarget) {
            targetReachable = true;
            break;
        }
    }

    if (targetReachable) {
        if (params[QStringLiteral("double")].toBool(false))
            QTest::mouseDClick(m_window, button, Qt::NoModifier, *point, 0);
        else
            QTest::mouseClick(m_window, button, Qt::NoModifier, *point, 0);
    } else {
        auto localCenter = QPointF(clickTarget->width() / 2.0, clickTarget->height() / 2.0);
        auto scenePos = clickTarget->mapToScene(localCenter);
        auto globalPos = clickTarget->mapToGlobal(localCenter);

        if (params[QStringLiteral("double")].toBool(false)) {
            QMouseEvent dblPress(QEvent::MouseButtonDblClick, localCenter, scenePos, globalPos,
                button, button, Qt::NoModifier);
            QMouseEvent dblRelease(QEvent::MouseButtonRelease, localCenter, scenePos, globalPos,
                button, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(clickTarget, &dblPress);
            QCoreApplication::sendEvent(clickTarget, &dblRelease);
        } else {
            QMouseEvent press(QEvent::MouseButtonPress, localCenter, scenePos, globalPos,
                button, button, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, localCenter, scenePos, globalPos,
                button, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(clickTarget, &press);
            QCoreApplication::sendEvent(clickTarget, &release);
        }
    }

    return QJsonObject {
        { QStringLiteral("clicked"), clickedName },
        { QStringLiteral("point"), QJsonArray { point->x(), point->y() } },
    };
}

// --- drag ---

QJsonObject CommandHandler::cmdDrag(const QJsonObject& params)
{
    if (!params.contains(QStringLiteral("from")) || !params.contains(QStringLiteral("to")))
        return makeErr(ErrInvalidParams, QStringLiteral("Missing 'from' and/or 'to' parameter"));

    QQuickItem* customRoot = nullptr;
    if (params.contains(QStringLiteral("root"))) {
        auto rootMatch = resolveItem(params[QStringLiteral("root")]);
        if (!rootMatch)
            return makeErr(rootMatch.error().code, rootMatch.error().message);
        customRoot = rootMatch->item;
    }

    auto fromMatch = resolveItem(params[QStringLiteral("from")], customRoot);
    if (!fromMatch)
        return makeErr(fromMatch.error().code, fromMatch.error().message);
    auto toMatch = resolveItem(params[QStringLiteral("to")], customRoot);
    if (!toMatch)
        return makeErr(toMatch.error().code, toMatch.error().message);

    const auto fromLabel = params[QStringLiteral("from")].isString()
        ? params[QStringLiteral("from")].toString()
        : QStringLiteral("<property query>");
    const auto toLabel = params[QStringLiteral("to")].isString()
        ? params[QStringLiteral("to")].toString()
        : QStringLiteral("<property query>");

    auto errFrom = checkAccessible(fromMatch->item, fromLabel, *m_finder);
    if (!errFrom.isEmpty())
        return errFrom;
    auto errTo = checkAccessible(toMatch->item, toLabel, *m_finder);
    if (!errTo.isEmpty())
        return errTo;

    auto fromPt = m_finder->clickPoint(*fromMatch->item);
    if (!fromPt)
        return makeErr(ErrClickFailed, QStringLiteral("Cannot get click point for drag source"));
    auto toPt = m_finder->clickPoint(*toMatch->item);
    if (!toPt)
        return makeErr(ErrClickFailed, QStringLiteral("Cannot get click point for drag target"));

    QTest::mousePress(m_window, Qt::LeftButton, Qt::NoModifier, *fromPt);
    QTest::mouseMove(m_window, *toPt, 100);
    QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier, *toPt);

    return QJsonObject {
        { QStringLiteral("dragged"), true },
        { QStringLiteral("from"), QJsonArray { fromPt->x(), fromPt->y() } },
        { QStringLiteral("to"), QJsonArray { toPt->x(), toPt->y() } },
    };
}

// --- swipe ---

QJsonObject CommandHandler::cmdSwipe(const QJsonObject& params)
{
    if (!params.contains(QStringLiteral("item")))
        return makeErr(ErrInvalidParams, QStringLiteral("Missing 'item' parameter"));

    auto match = resolveItem(params[QStringLiteral("item")]);
    if (!match)
        return makeErr(match.error().code, match.error().message);

    const auto identifier = params[QStringLiteral("item")].isString()
        ? params[QStringLiteral("item")].toString()
        : QStringLiteral("<property query>");
    auto errObj = checkAccessible(match->item, identifier, *m_finder);
    if (!errObj.isEmpty())
        return errObj;

    auto center = m_finder->clickPoint(*match->item);
    if (!center)
        return makeErr(ErrClickFailed, QStringLiteral("Cannot get center point for swipe"));

    const auto direction = params[QStringLiteral("direction")].toString(QStringLiteral("up"));
    const auto distance = params[QStringLiteral("distance")].toInt(200);

    QPoint delta;
    if (direction == QStringLiteral("up"))
        delta = { 0, -distance };
    else if (direction == QStringLiteral("down"))
        delta = { 0, distance };
    else if (direction == QStringLiteral("left"))
        delta = { -distance, 0 };
    else if (direction == QStringLiteral("right"))
        delta = { distance, 0 };
    else
        return makeErr(ErrInvalidParams, QStringLiteral("Invalid direction: %1 (use up/down/left/right)").arg(direction));

    // Simulate swipe as mouse press → move → release
    const auto startPt = *center;
    const auto endPt = startPt + delta;

    QTest::mousePress(m_window, Qt::LeftButton, Qt::NoModifier, startPt);
    // Intermediate steps for smooth scrolling
    constexpr int steps = 5;
    for (int i = 1; i <= steps; ++i) {
        const auto t = static_cast<double>(i) / steps;
        const auto intermediate = QPoint(
            startPt.x() + static_cast<int>(delta.x() * t),
            startPt.y() + static_cast<int>(delta.y() * t));
        QTest::mouseMove(m_window, intermediate, 20);
    }
    QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier, endPt);

    return QJsonObject {
        { QStringLiteral("swiped"), true },
        { QStringLiteral("direction"), direction },
        { QStringLiteral("distance"), distance },
        { QStringLiteral("from"), QJsonArray { startPt.x(), startPt.y() } },
        { QStringLiteral("to"), QJsonArray { endPt.x(), endPt.y() } },
    };
}

// --- keys ---

QJsonObject CommandHandler::cmdKeys(const QJsonObject& params)
{
    const auto text = params[QStringLiteral("text")].toString();
    const auto shortcut = params[QStringLiteral("shortcut")].toString();
    const auto key = params[QStringLiteral("key")].toString();

    QQuickItem* customRoot = nullptr;
    if (params.contains(QStringLiteral("root"))) {
        auto rootMatch = resolveItem(params[QStringLiteral("root")]);
        if (!rootMatch)
            return makeErr(rootMatch.error().code, rootMatch.error().message);
        customRoot = rootMatch->item;
    }

    if (params.contains(QStringLiteral("item"))) {
        auto match = resolveItem(params[QStringLiteral("item")], customRoot);
        if (!match)
            return makeErr(match.error().code, match.error().message);
        match->item->forceActiveFocus();
    }

    if (!shortcut.isEmpty()) {
        QTest::keySequence(m_window, QKeySequence(shortcut));
        QCoreApplication::processEvents();
        return QJsonObject { { QStringLiteral("shortcut"), shortcut } };
    }

    if (!key.isEmpty()) {
        static const std::map<QString, Qt::Key> keyMap = {
            { QStringLiteral("enter"), Qt::Key_Return },
            { QStringLiteral("return"), Qt::Key_Return },
            { QStringLiteral("escape"), Qt::Key_Escape },
            { QStringLiteral("tab"), Qt::Key_Tab },
            { QStringLiteral("backspace"), Qt::Key_Backspace },
            { QStringLiteral("delete"), Qt::Key_Delete },
            { QStringLiteral("up"), Qt::Key_Up },
            { QStringLiteral("down"), Qt::Key_Down },
            { QStringLiteral("left"), Qt::Key_Left },
            { QStringLiteral("right"), Qt::Key_Right },
            { QStringLiteral("home"), Qt::Key_Home },
            { QStringLiteral("end"), Qt::Key_End },
            { QStringLiteral("pageup"), Qt::Key_PageUp },
            { QStringLiteral("pagedown"), Qt::Key_PageDown },
            { QStringLiteral("space"), Qt::Key_Space },
            { QStringLiteral("f1"), Qt::Key_F1 },
            { QStringLiteral("f2"), Qt::Key_F2 },
            { QStringLiteral("f3"), Qt::Key_F3 },
            { QStringLiteral("f4"), Qt::Key_F4 },
            { QStringLiteral("f5"), Qt::Key_F5 },
        };

        auto it = keyMap.find(key.toLower());
        if (it == keyMap.end())
            return makeErr(ErrInvalidParams, QStringLiteral("Unknown key: %1").arg(key));

        QTest::keyClick(m_window, it->second);
        return QJsonObject { { QStringLiteral("key"), key } };
    }

    if (!text.isEmpty()) {
        for (const auto& ch : text) {
            QTest::keyClick(m_window, ch.toLatin1(), Qt::NoModifier, 0);
        }
        return QJsonObject { { QStringLiteral("typed"), text } };
    }

    return makeErr(ErrInvalidParams, QStringLiteral("Specify 'text', 'shortcut', or 'key'"));
}

// --- screenshot ---

QJsonObject CommandHandler::cmdScreenshot(const QJsonObject& params)
{
    QQuickItem* target = nullptr;
    if (params.contains(QStringLiteral("item"))) {
        auto match = resolveItem(params[QStringLiteral("item")]);
        if (!match)
            return makeErr(match.error().code, match.error().message);
        target = match->item;
    }

    auto image = m_window->grabWindow();
    if (image.isNull())
        return makeErr(ErrScreenshotFailed, QStringLiteral("Window grab returned null image"));

    if (target) {
        const auto rect = QRectF(target->mapToScene(QPointF(0, 0)),
            QSizeF(target->width(), target->height()));
        image = image.copy(rect.toAlignedRect());
    }

    QByteArray pngData;
    {
        QBuffer buffer(&pngData);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG"))
            return makeErr(ErrScreenshotFailed, QStringLiteral("Failed to encode screenshot as PNG"));
    }

    return QJsonObject {
        { QStringLiteral("width"), image.width() },
        { QStringLiteral("height"), image.height() },
        { QStringLiteral("format"), QStringLiteral("png") },
        { QStringLiteral("data"), QString::fromLatin1(pngData.toBase64()) },
    };
}

// --- tree ---

QJsonObject CommandHandler::cmdTree(const QJsonObject& params)
{
    const auto maxDepth = params[QStringLiteral("depth")].toInt(-1);
    const auto filter = parsePropsParam(params);
    if (!filter.error.isEmpty())
        return makeErr(ErrInvalidParams, filter.error);

    QQuickItem* root = nullptr;
    if (params.contains(QStringLiteral("root"))) {
        auto rootMatch = resolveItem(params[QStringLiteral("root")]);
        if (!rootMatch)
            return makeErr(rootMatch.error().code, rootMatch.error().message);
        root = rootMatch->item;
    }

    auto items = m_finder->findItems({}, false, false, maxDepth, root);

    QJsonArray arr;
    for (const auto& fr : items)
        arr.append(itemToJson(fr.item, fr.path, filter));

    return QJsonObject { { QStringLiteral("items"), arr } };
}

// --- wait ---

void CommandHandler::cmdWait(const QJsonValue& id, const QJsonObject& params, const std::function<void(const QJsonObject&)>& respond)
{
    if (!params.contains(QStringLiteral("item"))) {
        respond(jsonRpcError(id, ErrInvalidParams, QStringLiteral("Missing 'item' parameter")));
        return;
    }

    const auto identifier = params[QStringLiteral("item")].toString();
    const auto property = params[QStringLiteral("property")].toString();
    const auto expectedValue = params[QStringLiteral("value")].toVariant();
    const auto timeoutMs = params[QStringLiteral("timeout")].toInt(5000);

    auto match = m_finder->findItem(identifier);
    if (match && property.isEmpty()) {
        respond(jsonRpcResult(id, QJsonObject { { QStringLiteral("found"), identifier } }));
        return;
    }

    const auto waitKey = m_nextWaitId++;
    auto& pending = m_pendingWaits[waitKey];
    pending = std::make_unique<PendingWait>(waitKey, [id, respond](const QJsonObject& response) {
        auto stamped = response;
        stamped[QStringLiteral("id")] = id;
        respond(stamped);
    });

    connect(pending.get(), &PendingWait::finished, this, [this](int key) { m_pendingWaits.erase(key); }, Qt::QueuedConnection);

    if (match) {
        pending->waitForProperty(match->item, property, expectedValue, timeoutMs);
    } else {
        pending->waitForItem(m_finder, identifier, property, expectedValue, timeoutMs, m_finder->contentRoot(), m_finder->overlayRoot());
    }
}

// --- quit ---

QJsonObject CommandHandler::cmdQuit()
{
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    return QJsonObject { { QStringLiteral("exiting"), true } };
}

} // namespace chuck
