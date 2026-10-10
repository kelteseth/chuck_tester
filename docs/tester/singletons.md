# Addressing QML Singletons

By default, `tester` walks `QQuickWindow::contentItem()` and the popup overlay to resolve items by `objectName`. QML singletons (`pragma Singleton`, `QML_SINGLETON`, or `qmlRegisterSingletonInstance`) live on the `QQmlEngine` — not in the scene graph — so they're invisible to a normal `find`/`get`/`wait`.

Tester reaches them via a special identifier prefix.

## Syntax

```
@URI/Type[.prop.prop...]
```

- `@` is the singleton marker.
- The first `/` separates the QML import URI from the type name. (Using `/` instead of `.` between URI and type avoids ambiguity for dotted URIs like `MyApp.Components`.)
- Dotted segments after the type dereference `QObject*` properties recursively, so you can walk into nested objects exposed by the singleton without a separate `find` call.

## Examples

```python
# Read top-level properties of a registered singleton
await app.get("@MyApp/AppState", props="counter,message")

# Dereference a QObject* property exposed by the singleton
await app.get("@MyApp/AppState.timelineManager", props="activeIndex,selectedIndex")

# Wait on a property change
await app.wait(
    "@MyApp/AppState.timelineManager",
    property="activeIndex",
    value=1,
    timeout=5000,
)
```

For a URI containing dots, the `/` separator stays unambiguous:

```python
await app.get("@MyApp.Components/Theme", props="primaryColor")
```

## What works on singletons

| Command | Singleton support | Notes |
|---|---|---|
| `get`   | ✅ | Reads any `Q_PROPERTY` via the meta-object API |
| `wait`  | ✅ | Uses NOTIFY signal when available, polling fallback otherwise |
| `set`   | ✅ | Writes any writable property |
| `invoke` | ✅ | Calls `Q_INVOKABLE`s, slots and QML functions |
| `click`, `keys`, `drag`, `swipe`, `hover`, `scroll`, `screenshot` | ❌ | Returns error `-32010 NotInteractive` — singletons aren't `QQuickItem`s |
| `find`, `tree` | ❌ | These walk the scene graph; the engine has no public "list all singletons" API |

## Requirements

Tester needs access to the `QQmlEngine` that registered the singleton.

```cpp
QQmlApplicationEngine engine;
engine.loadFromModule("MyApp", "Main");

auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());

tester::TesterServer server(*window, { .port = 1408, .engine = &engine });
server.start();
```

If you omit `.engine`, tester falls back to `qmlEngine(window)`, which works when the window itself is a QML object loaded by the engine. If the window was created in C++ and QML loaded into it some other way, pass the engine explicitly — otherwise singleton lookups fail with a clear error message.

## When to use the `@` syntax vs a hidden helper

Use `@URI/Type` directly when:

- The singleton is registered (you control the C++ side).
- You only need to **read** properties or **wait** on changes.

Use a hidden `Item { objectName: "probe"; property var x: AppState.x }` mirror instead when:

- You need to interact with the singleton's data through the UI (e.g., click a button bound to the singleton's signal).
- The singleton exposes data only through `Q_INVOKABLE` methods that return non-trivial types — `invoke` is not yet a supported command.
- You want the test surface to stay independent of internal registration details.

Both approaches can coexist in the same app.

## Failure modes

| Error code | Condition |
|---|---|
| `-32001 ItemNotFound` | Singleton not registered, or a dotted property returned null / non-QObject |
| `-32007 PropertyNotFound` | Dotted segment isn't a property of the current object |
| `-32010 NotInteractive` | Tried to `click`/`keys`/`drag`/`swipe`/`hover`/`scroll`/`screenshot` on a singleton |
| `-32602 InvalidParams` | Identifier malformed (e.g., `@URI` without a `/Type`) |
