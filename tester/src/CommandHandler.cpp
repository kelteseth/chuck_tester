#include "CommandHandler.h"
#include "ItemFinder.h"
#include "PendingWait.h"
#include "SourceLocation.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaProperty>
#include <QMouseEvent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>

namespace {

enum class PropsMode : std::uint8_t { All,
    Custom,
    Named,
    Minimal };

QJsonObject collectProperties(QObject* object, PropsMode mode, const QStringList& names = {})
{
    if (mode == PropsMode::Minimal)
        return {};

    QJsonObject props;
    const auto* meta = object->metaObject();

    if (mode == PropsMode::Named) {
        for (const auto& name : names) {
            const auto value = object->property(name.toLatin1().constData());
            if (!value.isValid())
                continue;
            auto jv = QJsonValue::fromVariant(value);
            if (!jv.isUndefined())
                props[name] = jv;
        }
        return props;
    }

    // Custom mode strips QQuickItem's base properties so callers can see only
    // app-defined ones. For non-QQuickItems (singletons) "custom" is meaningless,
    // so it degrades to "all".
    const int startIdx = (mode == PropsMode::Custom && qobject_cast<QQuickItem*>(object))
        ? QQuickItem::staticMetaObject.propertyCount()
        : 0;

    for (int i = startIdx; i < meta->propertyCount(); ++i) {
        const auto prop = meta->property(i);
        auto jv = QJsonValue::fromVariant(prop.read(object));
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

QString checkPropsConflict(QObject* object, const PropsFilter& filter)
{
    if (filter.matchedKeyword.isEmpty())
        return {};
    if (object->metaObject()->indexOfProperty(filter.matchedKeyword.toLatin1().constData()) >= 0)
        return QStringLiteral("Item has property '%1' which conflicts with reserved mode keyword '%2'")
            .arg(filter.matchedKeyword, filter.matchedKeyword.toUpper());
    return {};
}

QJsonObject itemToJson(QObject* object, const QString& path, const PropsFilter& filter)
{
    QJsonObject obj {
        { QStringLiteral("objectName"), object->objectName() },
        { QStringLiteral("type"), QString::fromLatin1(object->metaObject()->className()) },
        { QStringLiteral("path"), path },
    };
    if (const auto source = tester::sourceLocation(object); !source.isEmpty())
        obj[QStringLiteral("source")] = source;

    if (filter.mode == PropsMode::Minimal) {
        const auto mandatory = object->property("isMandatory");
        if (mandatory.isValid() && mandatory.toBool())
            obj[QStringLiteral("isMandatory")] = true;
        return obj;
    }

    auto props = collectProperties(object, filter.mode, filter.names);
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

// Unlike topmostMouseItemAt this finds what is painted at the point, not what
// takes the click: any visible item counts, and children outside an unclipped
// parent's bounds still count, as they do on screen.
QQuickItem* topmostVisibleItemAt(QQuickItem* root, const QPointF& scenePos)
{
    if (!root->isVisible() || qFuzzyIsNull(root->opacity()))
        return nullptr;

    const bool inside = root->contains(root->mapFromScene(scenePos));
    if (!inside && root->clip())
        return nullptr;

    auto children = root->childItems();
    std::ranges::stable_sort(children, {}, &QQuickItem::z);
    for (auto i = children.size() - 1; i >= 0; --i) {
        if (auto* hit = topmostVisibleItemAt(children[i], scenePos))
            return hit;
    }
    return inside ? root : nullptr;
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

// Returns a populated error if `match` resolved to a non-QQuickItem (e.g. a singleton).
// Input commands (click/keys/drag/swipe/screenshot) require a real QQuickItem.
QJsonObject requireInteractive(const tester::FindResult& match, const QString& identifier, const char* command)
{
    if (match.item)
        return {};
    return makeErr(tester::ErrNotInteractive,
        QStringLiteral("'%1' resolved to a non-interactive target (%2) — '%3' requires a QQuickItem")
            .arg(identifier, QString::fromLatin1(match.object ? match.object->metaObject()->className() : "null"), QString::fromLatin1(command)));
}

} // namespace

namespace tester {

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

CommandHandler::CommandHandler(QQuickWindow& window, std::shared_ptr<ItemFinder> finder, bool allowScripting)
    : m_window { &window }
    , m_finder { std::move(finder) }
    , m_allowScripting { allowScripting }
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

// Resolves params[key] (scoped by an optional "root") to a QQuickItem that can
// receive input right now. On failure returns the error object to respond with.
std::expected<QQuickItem*, QJsonObject> CommandHandler::resolveInteractive(const QJsonObject& params, const QString& key, const char* command)
{
    QQuickItem* customRoot = nullptr;
    if (params.contains(QStringLiteral("root"))) {
        auto rootMatch = resolveItem(params[QStringLiteral("root")]);
        if (!rootMatch)
            return std::unexpected(makeErr(rootMatch.error().code, rootMatch.error().message));
        if (auto guard = requireInteractive(*rootMatch, QStringLiteral("<root>"), command); !guard.isEmpty())
            return std::unexpected(guard);
        customRoot = rootMatch->item;
    }

    auto match = resolveItem(params[key], customRoot);
    if (!match)
        return std::unexpected(makeErr(match.error().code, match.error().message));

    const auto identifier = params[key].isString() ? params[key].toString() : QStringLiteral("<property query>");
    if (auto guard = requireInteractive(*match, identifier, command); !guard.isEmpty())
        return std::unexpected(guard);
    if (auto errObj = checkAccessible(match->item, identifier, *m_finder); !errObj.isEmpty())
        return std::unexpected(errObj);
    return match->item;
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

    if (method == QStringLiteral("version"))
        result = cmdVersion();
    else if (method == QStringLiteral("find"))
        result = cmdFind(params);
    else if (method == QStringLiteral("get"))
        result = cmdGet(params);
    else if (method == QStringLiteral("click"))
        result = cmdClick(params);
    else if (method == QStringLiteral("drag"))
        result = cmdDrag(params);
    else if (method == QStringLiteral("swipe"))
        result = cmdSwipe(params);
    else if (method == QStringLiteral("hover"))
        result = cmdHover(params);
    else if (method == QStringLiteral("scroll"))
        result = cmdScroll(params);
    else if (method == QStringLiteral("window"))
        result = cmdWindow(params);
    else if (method == QStringLiteral("set"))
        result = cmdSet(params);
    else if (method == QStringLiteral("invoke"))
        result = cmdInvoke(params);
    else if (method == QStringLiteral("keys"))
        result = cmdKeys(params);
    else if (method == QStringLiteral("screenshot"))
        result = cmdScreenshot(params);
    else if (method == QStringLiteral("tree"))
        result = cmdTree(params);
    else if (method == QStringLiteral("itemAt"))
        result = cmdItemAt(params);
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

// --- version ---

QJsonObject CommandHandler::cmdVersion() const
{
    // Clients compare against "methods", not the protocol number, to decide
    // what they may call - keep this list in sync with handleCommand.
    QJsonArray methods {
        QStringLiteral("version"), QStringLiteral("find"), QStringLiteral("get"),
        QStringLiteral("click"), QStringLiteral("drag"), QStringLiteral("swipe"),
        QStringLiteral("hover"), QStringLiteral("scroll"), QStringLiteral("window"),
        QStringLiteral("keys"), QStringLiteral("screenshot"), QStringLiteral("tree"),
        QStringLiteral("wait"), QStringLiteral("quit"), QStringLiteral("itemAt")
    };
    if (m_allowScripting) {
        methods.append(QStringLiteral("set"));
        methods.append(QStringLiteral("invoke"));
    }
    return QJsonObject {
        { QStringLiteral("protocol"), ProtocolVersion },
        { QStringLiteral("tester"), QStringLiteral(TESTER_VERSION) },
        { QStringLiteral("qt"), QString::fromLatin1(qVersion()) },
        { QStringLiteral("methods"), methods },
        { QStringLiteral("scripting"), m_allowScripting },
        { QStringLiteral("pid"), QCoreApplication::applicationPid() },
        { QStringLiteral("sourceLocations"), hasSourceLocations() },
    };
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
        if (!rootMatch->item)
            return makeErr(ErrNotInteractive,
                QStringLiteral("'root' must be a QQuickItem — singletons can't be used as a search root"));
        customRoot = rootMatch->item;
    }

    auto items = m_finder->findItems(typeFilter, interactiveOnly, accessibleOnly, maxDepth, customRoot);

    if (filter.mode == PropsMode::Named && !items.empty()) {
        QStringList neverFound;
        for (const auto& name : filter.names) {
            const auto nameUtf8 = name.toLatin1();
            const auto found = std::ranges::any_of(items, [&](const auto& fr) {
                return fr.object->metaObject()->indexOfProperty(nameUtf8.constData()) >= 0;
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
        const auto conflict = checkPropsConflict(fr.object, filter);
        if (!conflict.isEmpty())
            return makeErr(ErrInvalidParams, conflict);
        arr.append(itemToJson(fr.object, fr.path, filter));
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
    const auto conflict = checkPropsConflict(match->object, filter);
    if (!conflict.isEmpty())
        return makeErr(ErrInvalidParams, conflict);
    if (filter.mode == PropsMode::Named) {
        const auto* meta = match->object->metaObject();
        QStringList unknown;
        for (const auto& name : filter.names) {
            if (meta->indexOfProperty(name.toLatin1().constData()) < 0)
                unknown.append(name);
        }
        if (!unknown.isEmpty())
            return makeErr(ErrPropertyNotFound,
                QStringLiteral("Unknown properties: %1").arg(unknown.join(QStringLiteral(", "))));
    }

    return itemToJson(match->object, match->path, filter);
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
        if (auto guard = requireInteractive(*rootMatch, QStringLiteral("<root>"), "click"); !guard.isEmpty())
            return guard;
        customRoot = rootMatch->item;
    }

    auto match = resolveItem(params[QStringLiteral("item")], customRoot);
    if (!match)
        return makeErr(match.error().code, match.error().message);

    const auto identifier = params[QStringLiteral("item")].isString()
        ? params[QStringLiteral("item")].toString()
        : QStringLiteral("<property query>");
    if (auto guard = requireInteractive(*match, identifier, "click"); !guard.isEmpty())
        return guard;
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

    // Hit-test for diagnostics only — never substitute a different delivery path.
    // Direct QCoreApplication::sendEvent bypasses QML's input dispatch, so handlers
    // wired via MouseArea/Button onClicked silently miss. Always go through the
    // window so the scene's real hit testing and event propagation run.
    if (auto* topmost = topmostMouseItemAt(m_window->contentItem(), *point); topmost) {
        bool reachable = false;
        for (auto* cur = topmost; cur; cur = cur->parentItem()) {
            if (cur == clickTarget) {
                reachable = true;
                break;
            }
        }
        if (!reachable) {
            qWarning() << "[tester] click point" << *point << "hits"
                       << QString::fromLatin1(topmost->metaObject()->className())
                       << "topmost — target" << clickedName << "may be covered";
        }
    }

    if (params[QStringLiteral("double")].toBool(false))
        QTest::mouseDClick(m_window, button, Qt::NoModifier, *point, 0);
    else
        QTest::mouseClick(m_window, button, Qt::NoModifier, *point, 0);

    return QJsonObject {
        { QStringLiteral("clicked"), clickedName },
        { QStringLiteral("point"), QJsonArray { point->x(), point->y() } },
    };
}

// --- drag ---

QJsonObject CommandHandler::cmdDrag(const QJsonObject& params)
{
    // Target is either another item ("to") or a pixel offset from the source
    // ("by": [dx, dy]). The offset form is what slider/timeline-handle tests
    // need - there is usually no item sitting at the drop position.
    const bool hasTo = params.contains(QStringLiteral("to"));
    const bool hasBy = params.contains(QStringLiteral("by"));
    if (!params.contains(QStringLiteral("from")) || (!hasTo && !hasBy))
        return makeErr(ErrInvalidParams, QStringLiteral("Missing 'from' and either 'to' or 'by' parameter"));
    if (hasTo && hasBy)
        return makeErr(ErrInvalidParams, QStringLiteral("'to' and 'by' are mutually exclusive"));

    QQuickItem* customRoot = nullptr;
    if (params.contains(QStringLiteral("root"))) {
        auto rootMatch = resolveItem(params[QStringLiteral("root")]);
        if (!rootMatch)
            return makeErr(rootMatch.error().code, rootMatch.error().message);
        if (auto guard = requireInteractive(*rootMatch, QStringLiteral("<root>"), "drag"); !guard.isEmpty())
            return guard;
        customRoot = rootMatch->item;
    }

    auto fromMatch = resolveItem(params[QStringLiteral("from")], customRoot);
    if (!fromMatch)
        return makeErr(fromMatch.error().code, fromMatch.error().message);

    const auto fromLabel = params[QStringLiteral("from")].isString()
        ? params[QStringLiteral("from")].toString()
        : QStringLiteral("<property query>");
    if (auto guard = requireInteractive(*fromMatch, fromLabel, "drag"); !guard.isEmpty())
        return guard;
    auto errFrom = checkAccessible(fromMatch->item, fromLabel, *m_finder);
    if (!errFrom.isEmpty())
        return errFrom;

    auto fromPt = m_finder->clickPoint(*fromMatch->item);
    if (!fromPt)
        return makeErr(ErrClickFailed, QStringLiteral("Cannot get click point for drag source"));

    QPoint endPt;
    if (hasTo) {
        auto toMatch = resolveItem(params[QStringLiteral("to")], customRoot);
        if (!toMatch)
            return makeErr(toMatch.error().code, toMatch.error().message);
        const auto toLabel = params[QStringLiteral("to")].isString()
            ? params[QStringLiteral("to")].toString()
            : QStringLiteral("<property query>");
        if (auto guard = requireInteractive(*toMatch, toLabel, "drag"); !guard.isEmpty())
            return guard;
        auto errTo = checkAccessible(toMatch->item, toLabel, *m_finder);
        if (!errTo.isEmpty())
            return errTo;
        auto toPt = m_finder->clickPoint(*toMatch->item);
        if (!toPt)
            return makeErr(ErrClickFailed, QStringLiteral("Cannot get click point for drag target"));
        endPt = *toPt;
    } else {
        const auto byArr = params[QStringLiteral("by")].toArray();
        if (byArr.size() != 2 || !byArr[0].isDouble() || !byArr[1].isDouble())
            return makeErr(ErrInvalidParams, QStringLiteral("'by' must be a [dx, dy] number array"));
        endPt = *fromPt + QPoint(byArr[0].toInt(), byArr[1].toInt());
    }

    // Multiple incremental moves, not one jump: DragHandler (and most
    // gesture recognizers) only engage once consecutive moves exceed the
    // drag threshold - a single press->move->release reads as a click with
    // a long hold, never as a drag.
    const int steps = std::clamp(params[QStringLiteral("steps")].toInt(8), 2, 100);

    QTest::mousePress(m_window, Qt::LeftButton, Qt::NoModifier, *fromPt);
    if (*fromPt != endPt) {
        const QPoint delta = endPt - *fromPt;
        for (int i = 1; i <= steps; ++i) {
            const auto t = static_cast<double>(i) / steps;
            const auto intermediate = QPoint(
                fromPt->x() + static_cast<int>(std::lround(delta.x() * t)),
                fromPt->y() + static_cast<int>(std::lround(delta.y() * t)));
            QTest::mouseMove(m_window, intermediate, 20);
        }
    }
    QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier, endPt);

    return QJsonObject {
        { QStringLiteral("dragged"), true },
        { QStringLiteral("steps"), steps },
        { QStringLiteral("from"), QJsonArray { fromPt->x(), fromPt->y() } },
        { QStringLiteral("to"), QJsonArray { endPt.x(), endPt.y() } },
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
    if (auto guard = requireInteractive(*match, identifier, "swipe"); !guard.isEmpty())
        return guard;
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

// --- hover ---

QJsonObject CommandHandler::cmdHover(const QJsonObject& params)
{
    // Moves the mouse with no button held. Target is an item's center (plus an
    // optional "by" offset) or an absolute window point - the latter is how a
    // test moves the pointer off an item again to end the hover.
    const bool hasItem = params.contains(QStringLiteral("item"));
    const bool hasPoint = params.contains(QStringLiteral("point"));
    if (hasItem == hasPoint)
        return makeErr(ErrInvalidParams, QStringLiteral("Specify exactly one of 'item' or 'point'"));

    QPoint point;
    QString hovered;
    if (hasItem) {
        auto item = resolveInteractive(params, QStringLiteral("item"), "hover");
        if (!item)
            return item.error();
        auto center = m_finder->clickPoint(**item);
        if (!center)
            return makeErr(ErrClickFailed, QStringLiteral("Cannot get hover point for item"));
        point = *center;
        if (params.contains(QStringLiteral("by"))) {
            const auto byArr = params[QStringLiteral("by")].toArray();
            if (byArr.size() != 2 || !byArr[0].isDouble() || !byArr[1].isDouble())
                return makeErr(ErrInvalidParams, QStringLiteral("'by' must be a [dx, dy] number array"));
            point += QPoint(byArr[0].toInt(), byArr[1].toInt());
        }
        hovered = (*item)->objectName();
    } else {
        const auto ptArr = params[QStringLiteral("point")].toArray();
        if (ptArr.size() != 2 || !ptArr[0].isDouble() || !ptArr[1].isDouble())
            return makeErr(ErrInvalidParams, QStringLiteral("'point' must be an [x, y] number array"));
        point = QPoint(ptArr[0].toInt(), ptArr[1].toInt());
    }

    QTest::mouseMove(m_window, point, 0);

    QJsonObject result { { QStringLiteral("point"), QJsonArray { point.x(), point.y() } } };
    if (hasItem)
        result[QStringLiteral("hovered")] = hovered;
    return result;
}

// --- scroll ---

QJsonObject CommandHandler::cmdScroll(const QJsonObject& params)
{
    if (!params.contains(QStringLiteral("item")))
        return makeErr(ErrInvalidParams, QStringLiteral("Missing 'item' parameter"));

    auto item = resolveInteractive(params, QStringLiteral("item"), "scroll");
    if (!item)
        return item.error();
    auto point = m_finder->clickPoint(**item);
    if (!point)
        return makeErr(ErrClickFailed, QStringLiteral("Cannot get scroll point for item"));

    // One wheel notch is 120 eighths of a degree. Positive y means the wheel
    // rolls away from the user, which scrolls content up.
    constexpr int notch = 120;
    const auto direction = params[QStringLiteral("direction")].toString(QStringLiteral("down"));
    QPoint angle;
    if (direction == QStringLiteral("up"))
        angle = { 0, notch };
    else if (direction == QStringLiteral("down"))
        angle = { 0, -notch };
    else if (direction == QStringLiteral("left"))
        angle = { notch, 0 };
    else if (direction == QStringLiteral("right"))
        angle = { -notch, 0 };
    else
        return makeErr(ErrInvalidParams, QStringLiteral("Invalid direction: %1 (use up/down/left/right)").arg(direction));

    const int steps = std::clamp(params[QStringLiteral("steps")].toInt(1), 1, 100);

    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    const auto modStr = params[QStringLiteral("modifiers")].toString();
    for (const auto& mod : modStr.split('+', Qt::SkipEmptyParts)) {
        const auto m = mod.trimmed().toLower();
        if (m == QStringLiteral("ctrl"))
            modifiers |= Qt::ControlModifier;
        else if (m == QStringLiteral("shift"))
            modifiers |= Qt::ShiftModifier;
        else if (m == QStringLiteral("alt"))
            modifiers |= Qt::AltModifier;
        else if (m == QStringLiteral("meta"))
            modifiers |= Qt::MetaModifier;
        else
            return makeErr(ErrInvalidParams, QStringLiteral("Unknown modifier: %1 (use Ctrl/Shift/Alt/Meta)").arg(mod));
    }

    // Sent to the window, not the item, so the scene's own hit testing picks
    // the receiver - same reasoning as click. Each event needs a fresh,
    // increasing timestamp: with the default of 0 Qt Quick treats every wheel
    // event after the first as stale and drops it.
    static QElapsedTimer clock;
    static quint64 lastTimestamp = 0;
    if (!clock.isValid())
        clock.start();
    const QPointF localPos(*point);
    const QPointF globalPos(m_window->mapToGlobal(*point));
    for (int i = 0; i < steps; ++i) {
        lastTimestamp = std::max(lastTimestamp + 1, static_cast<quint64>(clock.elapsed()));
        QWheelEvent event(localPos, globalPos, QPoint(), angle, Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
        event.setTimestamp(lastTimestamp);
        QCoreApplication::sendEvent(m_window, &event);
    }

    return QJsonObject {
        { QStringLiteral("scrolled"), (*item)->objectName() },
        { QStringLiteral("direction"), direction },
        { QStringLiteral("steps"), steps },
        { QStringLiteral("point"), QJsonArray { point->x(), point->y() } },
    };
}

// --- window ---

QJsonObject CommandHandler::windowInfo() const
{
    const auto states = m_window->windowStates();
    QString state = QStringLiteral("normal");
    if (states & Qt::WindowFullScreen)
        state = QStringLiteral("fullscreen");
    else if (states & Qt::WindowMinimized)
        state = QStringLiteral("minimized");
    else if (states & Qt::WindowMaximized)
        state = QStringLiteral("maximized");

    return QJsonObject {
        { QStringLiteral("state"), state },
        { QStringLiteral("frameless"), m_window->flags().testFlag(Qt::FramelessWindowHint) },
        { QStringLiteral("stayOnTop"), m_window->flags().testFlag(Qt::WindowStaysOnTopHint) },
        { QStringLiteral("visible"), m_window->isVisible() },
        { QStringLiteral("x"), m_window->x() },
        { QStringLiteral("y"), m_window->y() },
        { QStringLiteral("width"), m_window->width() },
        { QStringLiteral("height"), m_window->height() },
    };
}

QJsonObject CommandHandler::cmdWindow(const QJsonObject& params)
{
    // Everything is validated before anything is applied, so a bad request
    // never leaves the window half-changed.
    const auto stateStr = params[QStringLiteral("state")].toString();
    std::optional<Qt::WindowState> state;
    if (params.contains(QStringLiteral("state"))) {
        if (stateStr == QStringLiteral("normal"))
            state = Qt::WindowNoState;
        else if (stateStr == QStringLiteral("maximized"))
            state = Qt::WindowMaximized;
        else if (stateStr == QStringLiteral("minimized"))
            state = Qt::WindowMinimized;
        else if (stateStr == QStringLiteral("fullscreen"))
            state = Qt::WindowFullScreen;
        else
            return makeErr(ErrInvalidParams, QStringLiteral("Invalid state: %1 (use normal/maximized/minimized/fullscreen)").arg(stateStr));
    }

    for (const auto* key : { "frameless", "stayOnTop" }) {
        const auto k = QString::fromLatin1(key);
        if (params.contains(k) && !params[k].isBool())
            return makeErr(ErrInvalidParams, QStringLiteral("'%1' must be a bool").arg(k));
    }
    for (const auto* key : { "width", "height", "x", "y" }) {
        const auto k = QString::fromLatin1(key);
        if (params.contains(k) && !params[k].isDouble())
            return makeErr(ErrInvalidParams, QStringLiteral("'%1' must be a number").arg(k));
    }

    const bool hasSize = params.contains(QStringLiteral("width")) || params.contains(QStringLiteral("height"));
    const bool hasPos = params.contains(QStringLiteral("x")) || params.contains(QStringLiteral("y"));
    const int width = params[QStringLiteral("width")].toInt(m_window->width());
    const int height = params[QStringLiteral("height")].toInt(m_window->height());
    if (hasSize && (width <= 0 || height <= 0))
        return makeErr(ErrInvalidParams, QStringLiteral("'width' and 'height' must be positive integers"));
    // Maximized/fullscreen/minimized windows ignore geometry, so a size or
    // position only makes sense together with the normal state.
    if ((hasSize || hasPos) && state && *state != Qt::WindowNoState)
        return makeErr(ErrInvalidParams, QStringLiteral("Size/position can only be combined with state 'normal' (got '%1')").arg(stateStr));

    // Flags first: changing them can recreate the native window, which would
    // otherwise drop a state or geometry applied just before.
    auto flags = m_window->flags();
    if (params.contains(QStringLiteral("frameless")))
        flags.setFlag(Qt::FramelessWindowHint, params[QStringLiteral("frameless")].toBool());
    if (params.contains(QStringLiteral("stayOnTop")))
        flags.setFlag(Qt::WindowStaysOnTopHint, params[QStringLiteral("stayOnTop")].toBool());
    if (flags != m_window->flags()) {
        const bool wasVisible = m_window->isVisible();
        const bool wasNormal = !(m_window->windowStates() & (Qt::WindowMaximized | Qt::WindowFullScreen | Qt::WindowMinimized));
        const auto clientGeometry = m_window->geometry();
        m_window->setFlags(flags);
        // Some platforms hide the window when its flags change.
        if (wasVisible && !m_window->isVisible())
            m_window->show();
        // Windows keeps the outer frame rect when the frame is added/removed,
        // which grows or shrinks the client area. Tests care about the content
        // size, so put that back.
        if (wasNormal)
            m_window->setGeometry(clientGeometry);
    }

    if (state) {
        switch (*state) {
        case Qt::WindowMaximized:
            m_window->showMaximized();
            break;
        case Qt::WindowMinimized:
            m_window->showMinimized();
            break;
        case Qt::WindowFullScreen:
            m_window->showFullScreen();
            break;
        default:
            m_window->showNormal();
            break;
        }
    } else if ((hasSize || hasPos) && (m_window->windowStates() & (Qt::WindowMaximized | Qt::WindowFullScreen | Qt::WindowMinimized))) {
        // Implied: you can't resize a maximized window, so restore it first.
        m_window->showNormal();
    }

    if (hasSize)
        m_window->resize(width, height);
    if (hasPos)
        m_window->setPosition(params[QStringLiteral("x")].toInt(m_window->x()), params[QStringLiteral("y")].toInt(m_window->y()));
    QCoreApplication::processEvents();

    // min/max size constraints or the window manager may clamp or delay the
    // request, so report what the window actually ended up as.
    return windowInfo();
}

// --- set ---

QJsonObject CommandHandler::cmdSet(const QJsonObject& params)
{
    if (!m_allowScripting)
        return makeErr(ErrScriptingDisabled, QStringLiteral("'set' is disabled (TesterServer::Config::allowScripting)"));
    if (!params.contains(QStringLiteral("item")) || !params.contains(QStringLiteral("property")) || !params.contains(QStringLiteral("value")))
        return makeErr(ErrInvalidParams, QStringLiteral("Missing 'item', 'property' or 'value' parameter"));

    auto match = resolveItem(params[QStringLiteral("item")]);
    if (!match)
        return makeErr(match.error().code, match.error().message);

    // QQmlProperty rather than QObject::setProperty: it understands grouped
    // names ("font.pixelSize") and converts JSON numbers/strings to the
    // property's type (int, enum, color, ...).
    const auto name = params[QStringLiteral("property")].toString();
    auto* context = qmlContext(match->object);
    const auto prop = context ? QQmlProperty(match->object, name, context) : QQmlProperty(match->object, name);
    if (!prop.isValid())
        return makeErr(ErrPropertyNotFound, QStringLiteral("Unknown property: %1").arg(name));
    if (!prop.isWritable())
        return makeErr(ErrInvalidParams, QStringLiteral("Property is read-only: %1").arg(name));

    const auto previous = QJsonValue::fromVariant(prop.read());
    if (!prop.write(params[QStringLiteral("value")].toVariant()))
        return makeErr(ErrInvalidParams, QStringLiteral("Cannot write value to '%1' (type %2)").arg(name, QString::fromLatin1(prop.propertyMetaType().name())));

    return QJsonObject {
        { QStringLiteral("item"), match->object->objectName() },
        { QStringLiteral("property"), name },
        { QStringLiteral("previous"), previous },
        { QStringLiteral("value"), QJsonValue::fromVariant(prop.read()) },
    };
}

// --- invoke ---

QJsonObject CommandHandler::cmdInvoke(const QJsonObject& params)
{
    if (!m_allowScripting)
        return makeErr(ErrScriptingDisabled, QStringLiteral("'invoke' is disabled (TesterServer::Config::allowScripting)"));
    if (!params.contains(QStringLiteral("item")) || !params.contains(QStringLiteral("method")))
        return makeErr(ErrInvalidParams, QStringLiteral("Missing 'item' or 'method' parameter"));

    auto match = resolveItem(params[QStringLiteral("item")]);
    if (!match)
        return makeErr(match.error().code, match.error().message);

    auto* engine = qmlEngine(match->object);
    if (!engine)
        engine = m_finder->resolveEngine();
    if (!engine)
        return makeErr(ErrInvokeFailed, QStringLiteral("No QML engine available to invoke methods"));

    // Calling through the JS engine covers QML functions, Q_INVOKABLEs, slots
    // and signals alike, and lets the engine do the argument conversion.
    // toScriptValue (unlike newQObject) leaves the object's ownership alone.
    const auto method = params[QStringLiteral("method")].toString();
    const auto self = engine->toScriptValue(QVariant::fromValue(match->object));
    const auto fn = self.property(method);
    if (!fn.isCallable())
        return makeErr(ErrPropertyNotFound, QStringLiteral("No callable method '%1' on %2").arg(method, QString::fromLatin1(match->object->metaObject()->className())));

    QJSValueList args;
    for (const auto& arg : params[QStringLiteral("args")].toArray())
        args.append(engine->toScriptValue(arg.toVariant()));

    const auto ret = fn.callWithInstance(self, args);
    if (engine->hasError())
        return makeErr(ErrInvokeFailed, QStringLiteral("%1() threw: %2").arg(method, engine->catchError().toString()));
    if (ret.isError())
        return makeErr(ErrInvokeFailed, QStringLiteral("%1() threw: %2").arg(method, ret.toString()));

    QJsonValue result;
    if (auto* obj = ret.toQObject()) {
        result = QJsonObject {
            { QStringLiteral("objectName"), obj->objectName() },
            { QStringLiteral("type"), QString::fromLatin1(obj->metaObject()->className()) },
        };
    } else if (!ret.isUndefined()) {
        result = QJsonValue::fromVariant(ret.toVariant());
    }

    return QJsonObject {
        { QStringLiteral("item"), match->object->objectName() },
        { QStringLiteral("method"), method },
        { QStringLiteral("result"), result },
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
        if (auto guard = requireInteractive(*rootMatch, QStringLiteral("<root>"), "keys"); !guard.isEmpty())
            return guard;
        customRoot = rootMatch->item;
    }

    if (params.contains(QStringLiteral("item"))) {
        auto match = resolveItem(params[QStringLiteral("item")], customRoot);
        if (!match)
            return makeErr(match.error().code, match.error().message);
        const auto identifier = params[QStringLiteral("item")].isString()
            ? params[QStringLiteral("item")].toString()
            : QStringLiteral("<property query>");
        if (auto guard = requireInteractive(*match, identifier, "keys"); !guard.isEmpty())
            return guard;
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
        const auto identifier = params[QStringLiteral("item")].isString()
            ? params[QStringLiteral("item")].toString()
            : QStringLiteral("<property query>");
        if (auto guard = requireInteractive(*match, identifier, "screenshot"); !guard.isEmpty())
            return guard;
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
        if (!rootMatch->item)
            return makeErr(ErrNotInteractive,
                QStringLiteral("'root' must be a QQuickItem — singletons can't be used as a tree root"));
        root = rootMatch->item;
    }

    auto items = m_finder->findItems({}, false, false, maxDepth, root);

    QJsonArray arr;
    for (const auto& fr : items)
        arr.append(itemToJson(fr.object, fr.path, filter));

    return QJsonObject { { QStringLiteral("items"), arr } };
}

// --- itemAt ---

QJsonObject CommandHandler::cmdItemAt(const QJsonObject& params)
{
    if (!params.contains(QStringLiteral("x")) || !params.contains(QStringLiteral("y")))
        return makeErr(ErrInvalidParams, QStringLiteral("Missing 'x' or 'y' parameter"));
    const auto filter = parsePropsParam(params);
    if (!filter.error.isEmpty())
        return makeErr(ErrInvalidParams, filter.error);

    const QPointF point { params[QStringLiteral("x")].toDouble(), params[QStringLiteral("y")].toDouble() };
    auto* hit = topmostVisibleItemAt(m_window->contentItem(), point);
    if (!hit)
        return makeErr(ErrItemNotFound, QStringLiteral("No visible item at %1,%2").arg(point.x()).arg(point.y()));

    // Paths come from the same walk as find/tree so they can be passed back to
    // other commands. Items that walk skips (e.g. a control's internals) get none.
    std::unordered_map<QObject*, QString> paths;
    for (const auto& fr : m_finder->findItems())
        paths.emplace(fr.object, fr.path);
    const auto pathOf = [&paths](QObject* object) {
        const auto it = paths.find(object);
        return it == paths.end() ? QString {} : it->second;
    };

    QJsonArray ancestors;
    for (auto* cur = hit->parentItem(); cur; cur = cur->parentItem())
        ancestors.append(itemToJson(cur, pathOf(cur), { .mode = PropsMode::Minimal }));

    return QJsonObject {
        { QStringLiteral("item"), itemToJson(hit, pathOf(hit), filter) },
        { QStringLiteral("ancestors"), ancestors },
    };
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
        pending->waitForProperty(match->object, property, expectedValue, timeoutMs);
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

} // namespace tester
