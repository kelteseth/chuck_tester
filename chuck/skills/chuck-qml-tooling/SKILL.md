---
name: chuck-qml-tooling
description: 'Profile, debug, inspect and live-preview a tester-enabled Qt/QML app through chuck. Use when: a page or interaction is slow and you need to know why, you need the QML file:line behind something on screen, you want QML edits to show up in the running app without a rebuild, or you need to attach Qt Creator or a native debugger. Covers -qmljsdebugger, QmlProfiler / chuck profile / chuck qtd, QmlPreview / chuck preview (and Qt''s QmlPreviewMcpServer), item_at and the source field.'
---

# QML Tooling with Chuck: Profile, Inspect, Preview, Debug

Chuck drives the app over tester's WebSocket. Qt's QML tools use a second channel, the QML debug connector that Qt opens for `-qmljsdebugger=...`. Tester enables it, so no `QT_QML_DEBUG` build is needed. Full reference: `docs/guide/qml-tooling.md`.

## Pick the tool

| Goal | Tool | Cost |
|---|---|---|
| "Which QML file/line is this?" | `source` on `find`/`get`/`tree`; `item_at(x, y)` for a pixel | free, no relaunch |
| "What is at this point of the screenshot?" | `item_at(x, y)`, then the first ancestor with your app's `source` | free |
| Check or poke state | `get` / `set` / `invoke` / `wait` | free |
| "Why is this slow?" | `QmlProfiler` around the scenario, then `summarize()` / `chuck qtd` | relaunch with profiler args; app runs about 2× slower |
| Tune QML layout or styling | `QmlPreview` (`chuck preview`), or Qt's `QmlPreviewMcpServer` | relaunch under qmlpreview; QML only |
| JavaScript breakpoints | Qt Creator → *Attach to QML Port* (human in the loop) | relaunch with debugger args |
| C++ crash or hang | native debugger on `app.server["pid"]` | none |

Rules:
- Start with the free ones. Use `source` + `item_at` before guessing which file to edit.
- The debug connector takes **one** client: profile, preview or debug, never two at once. Chuck commands keep working next to any of them.
- C++, `qmldir`, CMake and new-file changes need a rebuild. Preview only helps for edits to existing QML files.

## Prerequisites (check once)

1. The app calls `tester::enableQmlDebuggingFromArguments(argc, argv)` first in `main()` (`#include <tester/QmlDebug.h>`). Without it, `-qmljsdebugger` is ignored and qmlprofiler/qmlpreview time out connecting. Ask before adding it to an app you don't own.
2. `app.server["protocol"] >= 3` for `item_at` and `source`; `app.server["sourceLocations"]` must be `true` for `source`. It is false when tester was built without `Qt6::QmlPrivate`.
3. Qt tools: pass `qt_bin=`, set `CHUCK_QT_BIN`, or use `qt_bin_from_build(build_dir)`.

## Inspect: from the screen to the source

```python
hit = await app.item_at(x, y, props="minimal")
own = next(i for i in [hit["item"], *hit["ancestors"]]
           if i.get("source") and "/qt-project.org/" not in i["source"])
own["source"]   # "qrc:/MyApp/Pages/Settings.qml:120:9" -> Pages/Settings.qml in the MyApp module's source dir
```

- `source` is where the object is **declared**. For `MyCard { }` in `Page.qml` it is that line in `Page.qml`. The card's own internals point into `MyCard.qml`.
- `qrc:/<prefix>/<URI path>/File.qml`: find `File.qml` in the module whose `qt_add_qml_module(URI ...)` matches.
- `qrc:/qt-project.org/...` and `qrc:/qt/qml/QtQuick/...` are Qt's own style files. Keep walking up the ancestors.

## Profile a scenario

```python
import subprocess
from chuck import ChuckClient, QmlProfiler, qmljsdebugger_arg
from chuck.qmldebug import PROFILER_SERVICES, free_port
from chuck.qtd import summarize

qml_port = free_port()
proc = subprocess.Popen([exe, "--tester-port=1408", qmljsdebugger_arg(qml_port, services=PROFILER_SERVICES)], env=env)
# ... wait for the tester port ...
with QmlProfiler(qml_port, qt_bin=qt_bin) as profiler:
    async with ChuckClient(port=1408) as app:
        await app.wait("projectList", property="visible", value=True)   # get to the start state first
        profiler.start()
        await app.click("openProjectButton")
        await app.wait("EntityPage", property="visible", value=True, timeout=60000)
        trace = profiler.save(out_dir / "open-project.qtd")
print(summarize(trace).format())
```

- Always pass `services=PROFILER_SERVICES`. The debugger service would turn off the JIT and distort JavaScript times.
- Recording is off until `start()`, so startup is not in the trace. For startup, use `qmljsdebugger_arg(..., block=True)` and `QmlProfiler(port, record=True)`.
- `save()` raises `QmlToolError("No data was recorded so far.")` when nothing QML ran in the window.
- MCP: `chuck_profile_start(qml_port)` and `chuck_profile_stop(output)`, which returns the summary.
- Shell: `uv run chuck profile --qml-port N -o t.qtd --duration 10`, then `uv run chuck qtd t.qtd`.

Reading the summary:
1. **Long top-level ranges** first. One long range right after the action is the culprit.
2. Re-run `summarize(trace, start_s=..., end_s=...)` over just that range. Self time by type and top by self time say where the time went.
3. Low self time on a long range means the time is in its children (object creation), not waiting.
4. `Compiling X.qml:0` = type loading, which includes the types it imports; don't sum nested ones. It only happens the first time per session.
5. `Creating X.qml:N` with a big count = bulk eager creation (all SwipeView tabs, unopened popups). Fix with `Loader`/`asynchronous`/lazy delegates.
6. Report shares (percent of the window), not absolute ms. The profiler roughly halves the speed.

For a deeper analysis against the sources, hand the `.qtd` to the `qt-qml-profiler` skill.

## Live preview

```python
from chuck import QmlPreview
with QmlPreview(exe, ["--tester-port=1408"], build_dir=build_dir, env=env) as preview:
    # edit QML; reconnect ChuckClient and screenshot to check the result
    preview.restart()   # when edits stop showing; reconnect ChuckClient afterwards
```

- `build_dir` is required for reloads of `qt_add_qml_module()` files. Chuck fixes the generated `.qrc` map that qmlpreview can't read directly.
- `close()` ends the app.
- Qt's `QmlPreviewMcpServer` (Qt 6.12 bin, MCP tool `run_qmlPreview`) also works with tester apps. Use it when only preview is needed. Use `QmlPreview` inside a chuck session or test.
- Multi-engine apps or QML on worker threads preview unreliably. If edits don't apply after `restart()`, rebuild.

## Debug

- QML/JS: launch with `qmljsdebugger_arg(port, services=DEBUGGER_SERVICES + INSPECTOR_SERVICES, block=True)` and tell the user to attach Qt Creator (*Debug → Start Debugging → Attach to QML Port*, `127.0.0.1:<port>`). An agent can't drive that UI. While a breakpoint holds the GUI thread, chuck calls time out.
- C++: `app.server["pid"]` → `gdb -p`, `lldb -p`, or Visual Studio *Attach to Process*.

## Failure modes

| Symptom | Cause |
|---|---|
| `qmlprofiler: timed out waiting for ['Connected to']` | App lacks `enableQmlDebuggingFromArguments`, wrong port, or another client holds the connector |
| `[tester] Not enabling QML debugging: ... needs host:127.0.0.1` in the app log | TCP argument without a loopback host; use `qmljsdebugger_arg()` |
| Preview connects but edits don't show | No `build_dir`/resource map, file not part of a QML module, or a change that needs a rebuild |
| `item_at` returns only Qt style items | You hit a control's internals; walk `ancestors` |
| No `source` field | Item created in C++, or `sourceLocations` is false |
| App logs missing on Windows | Qt logs to the debugger output for GUI apps; set `QT_FORCE_STDERR_LOGGING=1` |
