# Profiling, Debugging, Inspecting and Live Preview

Chuck drives the app through tester's WebSocket server. Qt's own QML tools (`qmlprofiler`, `qmlpreview`, Qt Creator's QML debugger) use a second channel: the **QML debug connector** that Qt opens for the `-qmljsdebugger` argument. Tester switches that connector on for you, so none of this needs a `QT_QML_DEBUG` build.

## Which tool for which job

| You want to | Use | Needs the debug connector |
|---|---|---|
| Find which QML file and line an item comes from | `source` on `find` / `get` / `tree`, or `item_at(x, y)` | no |
| Know what is drawn at a point (inspector pick) | `item_at(x, y)` | no |
| Read or change state while the app runs | `get` / `set` / `invoke` / `wait` | no |
| Find out why something is slow | `QmlProfiler` / `chuck profile`, then `chuck qtd` | yes, `PROFILER_SERVICES` |
| Iterate on QML layout or styling quickly | `QmlPreview` / `chuck preview`, or Qt's `QmlPreviewMcpServer` | yes, started by `qmlpreview` |
| Step through JavaScript, set breakpoints | Qt Creator → *Debug → Start Debugging → Attach to QML Port* | yes, `DEBUGGER_SERVICES` |
| Debug C++ | Any native debugger attached to `app.server["pid"]` | no |

The connector serves **one client at a time**: profile, debug or preview, not two together. Chuck's own commands keep working next to any of them.

## Enable it in the app

Call this first thing in `main()`, before the `QGuiApplication` and before any QML engine exists:

```cpp
#include <tester/QmlDebug.h>

int main(int argc, char* argv[])
{
    tester::enableQmlDebuggingFromArguments(argc, argv);
    QGuiApplication app(argc, argv);
    ...
}
```

It does nothing unless the app was started with `-qmljsdebugger=...`, so it is safe to leave in release builds. A TCP connector must name a loopback host (`host:127.0.0.1`, `localhost` or `::1`); otherwise Qt would listen on every network interface, and tester refuses with a warning. Local sockets (`file:`), which `qmlpreview` and Qt Creator use for local runs, are always accepted.

The app also needs Qt's `qmltooling` plugins. `windeployqt`/`macdeployqt` deploy them; an app run against a Qt install finds them there.

Build the argument with `qmljsdebugger_arg()`:

```python
from chuck.qmldebug import PROFILER_SERVICES, free_port, qmljsdebugger_arg

qml_port = free_port()
qmljsdebugger_arg(qml_port, services=PROFILER_SERVICES)
# -qmljsdebugger=port:51234,host:127.0.0.1,services:CanvasFrameRate,EngineControl,DebugMessages
qmljsdebugger_arg(qml_port, block=True)   # hold QML startup until a client attaches
```

Restrict `services` when profiling. The debugger service switches the JavaScript engine to the interpreter, which makes every JavaScript timing wrong.

The Qt tools are looked up in `qt_bin=`, then `$CHUCK_QT_BIN`, then `PATH`. `qt_bin_from_build(build_dir)` reads Qt's location from a CMake build directory.

## Source locations and picking items

With protocol 3, every item from `find`, `get`, `tree` and `item_at` has a `source` field when it was declared in QML:

```json
{"objectName": "greetBtn", "type": "Button_QMLTYPE_2", "path": "greetBtn",
 "source": "qrc:/MinimalExample/Main.qml:32:9"}
```

The location is where the object is **declared**. For an instance of your own component that is the line that instantiates it, not the component's file. The URL is the one the engine loaded; for a module built with `qt_add_qml_module()`, `qrc:/<prefix>/<URI path>/File.qml` is `File.qml` in that module's source directory.

`item_at(x, y)` returns what is drawn at a window point, the way Qt Creator's inspector picks it:

```python
hit = await app.item_at(200, 200, props="minimal")
hit["item"]["source"]   # "qrc:/qt-project.org/imports/QtQuick/Controls/Windows/Button.qml:12:17"
[a.get("objectName") for a in hit["ancestors"]]   # ["resetBtn", "", "ApplicationWindow", ...]
```

The hit is often a control's internal (its background or label, declared in Qt's style). The first ancestor with your `source` is usually the item to edit. Coordinates are in the same space as screenshots, so you can pick a pixel that looks wrong in a screenshot.

`source` needs tester built against `Qt6::QmlPrivate`. The location lives only in private QML data, the same place Qt Creator's inspector reads it from. Without it the field is missing and `app.server["sourceLocations"]` is `false`.

## Profiling

Launch the app with the profiler services, attach, and record only the scenario:

```python
import subprocess
from chuck import ChuckClient, QmlProfiler, qmljsdebugger_arg
from chuck.qmldebug import PROFILER_SERVICES, free_port
from chuck.qtd import summarize

qml_port = free_port()
proc = subprocess.Popen([exe, qmljsdebugger_arg(qml_port, services=PROFILER_SERVICES)])

with QmlProfiler(qml_port) as profiler:
    async with ChuckClient() as app:
        profiler.start()
        await app.click("openProjectButton")
        await app.wait("EntityPage", property="visible", value=True, timeout=60000)
        trace = profiler.save("open-project.qtd")

print(summarize(trace).format())
```

- `start()` / `stop()` toggle recording, and `save(path)` writes the trace and clears it. You can record several scenarios on one connection, and the app keeps running.
- `save()` raises `QmlToolError("No data was recorded so far.")` if the window recorded nothing.
- To profile **startup**, launch with `block=True` and create `QmlProfiler(qml_port, record=True)`. The engine waits for the profiler, so the trace starts at the first QML file.
- `features` defaults to `javascript, compiling, creating, binding, handlingsignal, pixmapcache`. Add `scenegraph`, `animations` or `memory` for rendering problems; the trace gets much larger.
- Under the profiler the app runs about twice as slowly. Compare shares, not absolute times.

From a shell, against an app already started with the argument:

```bash
uv run chuck profile --qml-port 51234 -o trace.qtd            # records until Enter
uv run chuck profile --qml-port 51234 -o trace.qtd --duration 10
```

### Reading a trace

```bash
uv run chuck qtd trace.qtd                      # whole trace
uv run chuck qtd trace.qtd --from 4.2 --to 9.8  # seconds from trace start
```

1. Read **long top-level ranges** first. One long range after the action is usually the culprit, e.g. a `.then()` callback that pushes a page.
2. Re-run with `--from`/`--to` set to that range. **Self time by type** and **top by self time** show what it spent the time on.
3. A long range with low self time does not wait on anything. The time is in its children, mostly object creation.
4. `Compiling Foo.qml:0` is type loading. It includes every type the file pulls in, so parents contain their children; don't add them up. It only happens on the first use per session.
5. `Creating Foo.qml:<line>` with a high count (`400x Button`) points at objects created eagerly in bulk: all tabs of a `SwipeView`, popups that are never opened.

The `.qtd` also opens in Qt Creator (*Analyze → QML Profiler → Load QML Trace*). For a deeper analysis against the sources, use the `qt-qml-profiler` skill from Qt's agent skills.

## Live preview

`qmlpreview` launches the app, watches the QML sources and pushes every saved change into the running app. Use it for layout, styling and binding work, where a rebuild costs more than the change.

```python
from chuck import QmlPreview

with QmlPreview(exe, ["--tester-port=1408"], build_dir="build/release") as preview:
    ...                  # edit QML files; chuck keeps driving the app on 1408
    preview.restart()    # relaunch and replay the input when edits stop showing
```

```bash
uv run chuck preview --build-dir build/release build/release/myapp.exe --tester-port=1408
```

- `build_dir` maps `qrc:` URLs back to the source files. Without that map nothing reloads. Chuck builds it from the `.qrc` files that `qt_add_qml_module()` generates, because `qmlpreview` (Qt 6.12) can't resolve those directly. Pass `resources=[...]` for resources that come from somewhere else.
- After `restart()` the tester port comes back with the new process: reconnect your `ChuckClient`.
- `close()` ends the app too.
- Still needs a rebuild: C++ changes, `qmldir`/URI/singleton changes, CMake changes, new QML files compiled into resources, and imports of modules the app doesn't link.
- Some edits apply but lose state, or don't show (turning a value into a binding, moving a binding). `restart()` when in doubt.
- Apps with more than one QML engine, or that load QML on a worker thread, don't preview reliably.

Qt 6.12 also ships `QmlPreviewMcpServer` (Qt Maintenance Tool → *Agentic Development Tools*), an MCP server around the same tool with one tool, `run_qmlPreview`. It works with tester-enabled apps as well, since `qmlpreview` passes the `file:` connector that tester accepts. Prefer it when the agent only needs preview. Prefer `QmlPreview` when the preview runs inside a chuck test or session.

## QML debugger

Launch with the debugger services and attach Qt Creator (*Debug → Start Debugging → Attach to QML Port*, host `127.0.0.1`):

```python
qmljsdebugger_arg(qml_port, services=DEBUGGER_SERVICES + INSPECTOR_SERVICES, block=True)
```

`block=True` holds QML startup until the debugger is attached, so breakpoints in startup code are hit. Breakpoints pause the GUI thread, and chuck commands time out while it is paused.

For C++, attach a native debugger to the process ID that `app.server["pid"]` reports (Visual Studio *Attach to Process*, `gdb -p`, `lldb -p`).
