#include "CommandHandler.h"
#include "ItemFinder.h"
#include "PendingWait.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
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

} // namespace

namespace chuck {

namespace {

    QJsonObject checkAccessible(QQuickItem* item, const QString& identifier, ItemFinder& finder,
        const std::function<QJsonObject(int, const QString&, const QString&, const QJsonObject&)>& makeError)
    {
        if (!item->isVisible())
            return makeError(0, QStringLiteral("item_not_visible"),
                QStringLiteral("Item not visible: %1").arg(identifier), {});
        if (!item->isEnabled())
            return makeError(0, QStringLiteral("item_disabled"),
                QStringLiteral("Item disabled: %1").arg(identifier), {});
        if (!item->window())
            return makeError(0, QStringLiteral("item_not_ready"),
                QStringLiteral("Item not attached to window: %1").arg(identifier), {});
        if (!finder.isOnScreen(*item))
            return makeError(0, QStringLiteral("item_not_on_screen"),
                QStringLiteral("Item not on screen: %1").arg(identifier), {});
        if (auto* modal = finder.activeModalPopup()) {
            if (!finder.isInsideModalPopup(item, modal))
                return makeError(0, QStringLiteral("item_blocked_by_modal"),
                    QStringLiteral("Item blocked by modal popup: %1").arg(identifier), {});
        }
        return {};
    }

} // namespace

CommandHandler::CommandHandler(QQuickWindow& window, std::shared_ptr<ItemFinder> finder)
    : m_window { &window }
    , m_finder { std::move(finder) }
{
}

void CommandHandler::handleCommand(const QJsonObject& request, const std::function<void(const QJsonObject&)>& respond)
{
    const auto id = request[QStringLiteral("id")].toInt(-1);
    const auto cmd = request[QStringLiteral("cmd")].toString();
    const auto params = request[QStringLiteral("params")].toObject();

    if (cmd.isEmpty()) {
        respond(error(id, QStringLiteral("invalid_command"), QStringLiteral("Missing 'cmd' field")));
        return;
    }

    auto result = QJsonObject {};

    if (cmd == QStringLiteral("find"))
        result = cmdFind(params);
    else if (cmd == QStringLiteral("get"))
        result = cmdGet(params);
    else if (cmd == QStringLiteral("click"))
        result = cmdClick(params);
    else if (cmd == QStringLiteral("keys"))
        result = cmdKeys(params);
    else if (cmd == QStringLiteral("wait")) {
        cmdWait(id, params, respond);
        return;
    } else if (cmd == QStringLiteral("quit"))
        result = cmdQuit();
    else {
        respond(error(id, QStringLiteral("unknown_command"), QStringLiteral("Unknown command: %1").arg(cmd)));
        return;
    }

    result[QStringLiteral("id")] = id;
    respond(result);
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
        return error(0, QStringLiteral("invalid_props"), filter.error);

    QQuickItem* customRoot = nullptr;
    if (params.contains(QStringLiteral("root"))) {
        auto rootMatch = m_finder->findItem(params[QStringLiteral("root")].toString());
        if (!rootMatch)
            return error(0, rootMatch.error().code, rootMatch.error().message);
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
            return error(0, QStringLiteral("unknown_properties"),
                QStringLiteral("No items have properties: %1").arg(neverFound.join(QStringLiteral(", "))));
    }

    QJsonArray arr;
    for (const auto& fr : items) {
        const auto conflict = checkPropsConflict(fr.item, filter);
        if (!conflict.isEmpty())
            return error(0, QStringLiteral("ambiguous_props"), conflict);
        arr.append(itemToJson(fr.item, fr.path, filter));
    }
    return {
        { QStringLiteral("ok"), true },
        { QStringLiteral("result"), arr },
    };
}

// --- get ---

QJsonObject CommandHandler::cmdGet(const QJsonObject& params)
{
    const auto identifier = params[QStringLiteral("item")].toString();
    if (identifier.isEmpty())
        return error(0, QStringLiteral("missing_param"), QStringLiteral("Missing 'item' parameter"));

    auto match = m_finder->findItem(identifier);
    if (!match)
        return error(0, match.error().code, match.error().message);

    const auto filter = parsePropsParam(params);
    if (!filter.error.isEmpty())
        return error(0, QStringLiteral("invalid_props"), filter.error);
    const auto conflict = checkPropsConflict(match->item, filter);
    if (!conflict.isEmpty())
        return error(0, QStringLiteral("ambiguous_props"), conflict);
    if (filter.mode == PropsMode::Named) {
        const auto* meta = match->item->metaObject();
        QStringList unknown;
        for (const auto& name : filter.names) {
            if (meta->indexOfProperty(name.toLatin1().constData()) < 0)
                unknown.append(name);
        }
        if (!unknown.isEmpty())
            return error(0, QStringLiteral("unknown_properties"),
                QStringLiteral("Unknown properties: %1").arg(unknown.join(QStringLiteral(", "))));
    }

    return {
        { QStringLiteral("ok"), true },
        { QStringLiteral("result"), itemToJson(match->item, match->path, filter) },
    };
}

// --- click ---

QJsonObject CommandHandler::cmdClick(const QJsonObject& params)
{
    QQuickItem* customRoot = nullptr;
    if (params.contains(QStringLiteral("root"))) {
        auto rootMatch = m_finder->findItem(params[QStringLiteral("root")].toString());
        if (!rootMatch)
            return error(0, rootMatch.error().code, rootMatch.error().message);
        customRoot = rootMatch->item;
    }

    // Drag variant: from -> to
    if (params.contains(QStringLiteral("from")) && params.contains(QStringLiteral("to"))) {
        auto fromMatch = m_finder->findItem(params[QStringLiteral("from")].toString(), customRoot);
        auto toMatch = m_finder->findItem(params[QStringLiteral("to")].toString(), customRoot);
        if (!fromMatch)
            return error(0, fromMatch.error().code, fromMatch.error().message);
        if (!toMatch)
            return error(0, toMatch.error().code, toMatch.error().message);

        auto errFrom = checkAccessible(fromMatch->item, params[QStringLiteral("from")].toString(), *m_finder,
            [this](int id, const QString& code, const QString& msg, const QJsonObject& extra) {
                return error(id, code, msg, extra);
            });
        if (!errFrom.isEmpty())
            return errFrom;
        auto errTo = checkAccessible(toMatch->item, params[QStringLiteral("to")].toString(), *m_finder,
            [this](int id, const QString& code, const QString& msg, const QJsonObject& extra) {
                return error(id, code, msg, extra);
            });
        if (!errTo.isEmpty())
            return errTo;

        auto fromPt = m_finder->clickPoint(*fromMatch->item);
        if (!fromPt)
            return error(0, QStringLiteral("click_failed"), QStringLiteral("Cannot get click point for drag source"));
        auto toPt = m_finder->clickPoint(*toMatch->item);
        if (!toPt)
            return error(0, QStringLiteral("click_failed"), QStringLiteral("Cannot get click point for drag target"));

        QTest::mousePress(m_window, Qt::LeftButton, Qt::NoModifier, *fromPt);
        QTest::mouseMove(m_window, *toPt, 100);
        QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier, *toPt);

        return {
            { QStringLiteral("ok"), true },
            {
                QStringLiteral("result"),
                QJsonObject {
                    { QStringLiteral("dragged"), true },
                    { QStringLiteral("from"), QJsonArray { fromPt->x(), fromPt->y() } },
                    { QStringLiteral("to"), QJsonArray { toPt->x(), toPt->y() } },
                },
            },
        };
    }

    // Normal click
    const auto identifier = params[QStringLiteral("item")].toString();
    if (identifier.isEmpty())
        return error(0, QStringLiteral("missing_param"), QStringLiteral("Missing 'item' parameter"));

    auto match = m_finder->findItem(identifier, customRoot);
    if (!match)
        return error(0, match.error().code, match.error().message);

    auto errObj = checkAccessible(match->item, identifier, *m_finder,
        [this](int id, const QString& code, const QString& msg, const QJsonObject& extra) {
            return error(id, code, msg, extra);
        });
    if (!errObj.isEmpty())
        return errObj;

    auto* clickTarget = match->item;
    if (clickTarget->acceptedMouseButtons() == Qt::NoButton) {
        if (auto* child = m_finder->firstInteractiveDescendant(clickTarget))
            clickTarget = child;
    }

    auto point = m_finder->clickPoint(*clickTarget);
    if (!point)
        return error(0, QStringLiteral("click_failed"), QStringLiteral("Cannot get click point for item"));
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
        return {
            { QStringLiteral("ok"), true },
            {
                QStringLiteral("result"),
                QJsonObject {
                    { QStringLiteral("clicked"), clickedName },
                    { QStringLiteral("hold"), holdMs },
                    { QStringLiteral("point"), QJsonArray { point->x(), point->y() } },
                },
            },
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

    return {
        { QStringLiteral("ok"), true },
        {
            QStringLiteral("result"),
            QJsonObject {
                { QStringLiteral("clicked"), clickedName },
                { QStringLiteral("point"), QJsonArray { point->x(), point->y() } },
            },
        },
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
        auto rootMatch = m_finder->findItem(params[QStringLiteral("root")].toString());
        if (!rootMatch)
            return error(0, rootMatch.error().code, rootMatch.error().message);
        customRoot = rootMatch->item;
    }

    const auto identifier = params[QStringLiteral("item")].toString();
    if (!identifier.isEmpty()) {
        auto match = m_finder->findItem(identifier, customRoot);
        if (!match)
            return error(0, match.error().code, match.error().message);
        match->item->forceActiveFocus();
    }

    if (!shortcut.isEmpty()) {
        QTest::keySequence(m_window, QKeySequence(shortcut));
        return {
            { QStringLiteral("ok"), true },
            {
                QStringLiteral("result"),
                QJsonObject {
                    { QStringLiteral("shortcut"), shortcut },
                },
            },
        };
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
            return error(0, QStringLiteral("unknown_key"), QStringLiteral("Unknown key: %1").arg(key));

        QTest::keyClick(m_window, it->second);
        return {
            { QStringLiteral("ok"), true },
            {
                QStringLiteral("result"),
                QJsonObject {
                    { QStringLiteral("key"), key },
                },
            },
        };
    }

    if (!text.isEmpty()) {
        for (const auto& ch : text) {
            QTest::keySequence(m_window, QKeySequence(ch));
        }

        return {
            { QStringLiteral("ok"), true },
            {
                QStringLiteral("result"),
                QJsonObject {
                    { QStringLiteral("typed"), text },
                },
            },
        };
    }

    return error(0, QStringLiteral("missing_param"), QStringLiteral("Specify 'text', 'shortcut', or 'key'"));
}

// --- wait ---

void CommandHandler::cmdWait(int id, const QJsonObject& params, const std::function<void(const QJsonObject&)>& respond)
{
    const auto identifier = params[QStringLiteral("item")].toString();
    const auto property = params[QStringLiteral("property")].toString();
    const auto expectedValue = params[QStringLiteral("value")].toVariant();
    const auto timeoutMs = params[QStringLiteral("timeout")].toInt(5000);

    if (identifier.isEmpty()) {
        respond(error(id, QStringLiteral("missing_param"), QStringLiteral("Missing 'item' parameter")));
        return;
    }

    auto match = m_finder->findItem(identifier);
    if (match && property.isEmpty()) {
        auto result = QJsonObject {
            { QStringLiteral("ok"), true },
            {
                QStringLiteral("result"),
                QJsonObject {
                    { QStringLiteral("found"), identifier },
                },
            },
        };
        result[QStringLiteral("id")] = id;
        respond(result);
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
    return {
        { QStringLiteral("ok"), true },
        { QStringLiteral("result"), QJsonObject {} },
    };
}

// --- helpers ---

QJsonObject CommandHandler::error(int id, const QString& code, const QString& message, const QJsonObject& extra)
{
    auto err = QJsonObject {
        { QStringLiteral("code"), code },
        { QStringLiteral("message"), message },
    };

    for (auto it = extra.begin(); it != extra.end(); ++it) {
        err[it.key()] = it.value();
    }

    return {
        { QStringLiteral("id"), id },
        { QStringLiteral("ok"), false },
        { QStringLiteral("error"), err },
    };
}

} // namespace chuck
