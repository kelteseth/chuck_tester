"""MCP server exposing chuck automation tools for AI agents.

Runs over stdio so that any MCP-capable AI (Claude, etc.) can drive
a tester-enabled Qt/QML application through the standard Model Context
Protocol.

Usage (standalone)::

    python -m chuck mcp --port 1408
    python -m chuck mcp --port 1408 --record session.jsonl

The server manages its own ChuckClient connection via the FastMCP
lifespan mechanism.
"""

from __future__ import annotations

import base64
import json
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager
from dataclasses import dataclass, field
from typing import Any

from mcp.server.fastmcp import FastMCP, Image

from .commands import ChuckClient
from .qmldebug import DEFAULT_PROFILE_FEATURES, QmlProfiler
from .qtd import summarize
from .recorder import Recorder


# ---------------------------------------------------------------------------
# Lifespan — connect/disconnect the ChuckClient
# ---------------------------------------------------------------------------

@dataclass
class AppContext:
    """Holds the live ChuckClient (and optional recorder) for MCP tools."""

    app: ChuckClient
    recorder: Recorder | None = None
    profiler: QmlProfiler | None = None


# These are set before mcp.run() is called via configure().
_server_host: str = "127.0.0.1"
_server_port: int = 1408
_record_path: str | None = None


def configure(*, host: str = "127.0.0.1", port: int = 1408, record: str | None = None) -> None:
    """Set connection parameters before starting the MCP server."""
    global _server_host, _server_port, _record_path
    _server_host = host
    _server_port = port
    _record_path = record


@asynccontextmanager
async def app_lifespan(server: FastMCP) -> AsyncIterator[AppContext]:
    """Connect to the tester server on startup, disconnect on shutdown."""
    app = ChuckClient(host=_server_host, port=_server_port)
    await app.__aenter__()

    recorder: Recorder | None = None
    if _record_path:
        recorder = Recorder(_record_path)
        recorder.start()

    context = AppContext(app=app, recorder=recorder)
    try:
        yield context
    finally:
        if context.profiler:
            context.profiler.close()
        if recorder:
            recorder.stop()
        await app.__aexit__(None, None, None)


# ---------------------------------------------------------------------------
# FastMCP server
# ---------------------------------------------------------------------------

mcp = FastMCP(
    "chuck",
    instructions=(
        "You are driving a Qt/QML desktop application through the tester "
        "automation library. Use the chuck_* tools to interact with UI "
        "elements by their objectName. Use chuck_tree or chuck_find to "
        "discover available elements first."
    ),
    lifespan=app_lifespan,
)


# ---------------------------------------------------------------------------
# Helper — optional recording
# ---------------------------------------------------------------------------

def _ctx_app(ctx) -> tuple[ChuckClient, Recorder | None]:
    """Extract the ChuckClient and optional Recorder from the request context."""
    lc: AppContext = ctx.request_context.lifespan_context
    return lc.app, lc.recorder


# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------

@mcp.tool()
async def chuck_find(
    ctx,
    type: str | None = None,
    interactive: bool = False,
    accessible: bool = False,
    depth: int | None = None,
    root: str | None = None,
    props: str | None = None,
) -> list[dict[str, Any]]:
    """Search the QML object tree for items matching the given criteria.

    Args:
        type: QML type name to filter by (e.g. "Button", "TextField").
        interactive: Only return items that accept input.
        accessible: Only return items with Accessible metadata.
        depth: Maximum tree depth to search.
        root: objectName of the root item to search from.
        props: Comma-separated property names to include in results.
    """
    app, rec = _ctx_app(ctx)
    kwargs: dict[str, Any] = dict(
        type=type, interactive=interactive, accessible=accessible,
        depth=depth, root=root, props=props,
    )
    if rec:
        return await rec.find(app, **kwargs)
    return await app.find(**kwargs)


@mcp.tool()
async def chuck_get(
    ctx,
    item: str,
    props: str | None = None,
) -> dict[str, Any]:
    """Read properties of a specific QML item.

    Args:
        item: objectName of the target item.
        props: Comma-separated property names to read.
    """
    app, rec = _ctx_app(ctx)
    if rec:
        return await rec.get(app, item, props=props)
    return await app.get(item, props=props)


@mcp.tool()
async def chuck_click(
    ctx,
    item: str,
    button: str | None = None,
    double: bool = False,
    hold: int | None = None,
    root: str | None = None,
) -> dict[str, Any]:
    """Click a QML item.

    Args:
        item: objectName of the item to click.
        button: Mouse button ("left", "right", "middle"). Default "left".
        double: Perform a double-click.
        hold: Hold duration in milliseconds before releasing.
        root: objectName of a root item to search from.
    """
    app, rec = _ctx_app(ctx)
    if rec:
        return await rec.click(app, item, button=button, double=double, hold=hold, root=root)
    return await app.click(item, button=button, double=double, hold=hold, root=root)


@mcp.tool()
async def chuck_keys(
    ctx,
    item: str | None = None,
    text: str | None = None,
    key: str | None = None,
    shortcut: str | None = None,
    root: str | None = None,
) -> dict[str, Any]:
    """Type text, send a special key, or a keyboard shortcut to a QML item.

    Args:
        item: objectName of the target item (optional for global shortcuts).
        text: Text string to type character-by-character.
        key: Special key name (e.g. "Return", "Escape", "Tab").
        shortcut: Keyboard shortcut (e.g. "Ctrl+S", "Ctrl+Z").
        root: objectName of a root item to search from.
    """
    app, rec = _ctx_app(ctx)
    if rec:
        return await rec.keys(app, item, text=text, key=key, shortcut=shortcut, root=root)
    return await app.keys(item, text=text, key=key, shortcut=shortcut, root=root)


@mcp.tool()
async def chuck_swipe(
    ctx,
    item: str,
    direction: str = "up",
    distance: int = 200,
) -> dict[str, Any]:
    """Perform a swipe/scroll gesture on a QML item.

    Args:
        item: objectName of the item to swipe on.
        direction: Swipe direction ("up", "down", "left", "right").
        distance: Swipe distance in pixels.
    """
    app, rec = _ctx_app(ctx)
    if rec:
        return await rec.swipe(app, item, direction=direction, distance=distance)
    return await app.swipe(item, direction=direction, distance=distance)


@mcp.tool()
async def chuck_drag(
    ctx,
    from_item: str,
    to_item: str | None = None,
    by: list[int] | None = None,
    steps: int | None = None,
    root: str | None = None,
) -> dict[str, Any]:
    """Drag from one QML item to another, or by a pixel offset.

    Args:
        from_item: objectName of the item to start the drag on.
        to_item: objectName of the item to drop on. Exclusive with *by*.
        by: [dx, dy] pixel offset from the source item's center. Exclusive with *to_item*.
        steps: Number of intermediate mouse moves (default 8).
        root: objectName of a root item to search from.
    """
    app, rec = _ctx_app(ctx)
    if rec:
        return await rec.drag(app, from_item, to_item, by=by, steps=steps, root=root)
    return await app.drag(from_item, to_item, by=by, steps=steps, root=root)


@mcp.tool()
async def chuck_hover(
    ctx,
    item: str | None = None,
    by: list[int] | None = None,
    point: list[int] | None = None,
    root: str | None = None,
) -> dict[str, Any]:
    """Move the mouse over a QML item (no button pressed) to trigger hover effects.

    To end a hover, hover another item or a window point.

    Args:
        item: objectName of the item to hover. Exclusive with *point*.
        by: [dx, dy] offset from the item's center.
        point: [x, y] window coordinates to move to instead of an item.
        root: objectName of a root item to search from.
    """
    app, rec = _ctx_app(ctx)
    if rec:
        return await rec.hover(app, item, by=by, point=point, root=root)
    return await app.hover(item, by=by, point=point, root=root)


@mcp.tool()
async def chuck_scroll(
    ctx,
    item: str,
    direction: str = "down",
    steps: int = 1,
    modifiers: str | None = None,
    root: str | None = None,
) -> dict[str, Any]:
    """Turn the mouse wheel over a QML item.

    Flickables animate wheel scrolling; use chuck_wait on contentY or
    moving=false before reading the final position.

    Args:
        item: objectName of the item under the wheel.
        direction: "up", "down", "left" or "right".
        steps: Number of wheel notches.
        modifiers: Held modifiers, e.g. "Ctrl" or "Ctrl+Shift".
        root: objectName of a root item to search from.
    """
    app, rec = _ctx_app(ctx)
    if rec:
        return await rec.scroll(app, item, direction=direction, steps=steps, modifiers=modifiers, root=root)
    return await app.scroll(item, direction=direction, steps=steps, modifiers=modifiers, root=root)


@mcp.tool()
async def chuck_window(
    ctx,
    state: str | None = None,
    frameless: bool | None = None,
    stay_on_top: bool | None = None,
    width: int | None = None,
    height: int | None = None,
    x: int | None = None,
    y: int | None = None,
) -> dict[str, Any]:
    """Change the application window's state, flags or geometry, e.g. to check layouts at different sizes.

    Call with no arguments to just read the current values. Returns the
    window's actual state/flags/geometry afterwards (the window manager may
    clamp or delay a request).

    Args:
        state: "normal", "maximized", "minimized" or "fullscreen".
        frameless: Remove (true) or restore (false) the title bar and borders.
        stay_on_top: Keep the window above other windows.
        width: New width in pixels (implies state "normal").
        height: New height in pixels (implies state "normal").
        x: New x position in screen coordinates.
        y: New y position in screen coordinates.
    """
    app, rec = _ctx_app(ctx)
    kwargs: dict[str, Any] = dict(
        state=state, frameless=frameless, stay_on_top=stay_on_top,
        width=width, height=height, x=x, y=y,
    )
    if rec:
        return await rec.window(app, **kwargs)
    return await app.window(**kwargs)


@mcp.tool()
async def chuck_set(ctx, item: str, property: str, value: Any) -> dict[str, Any]:
    """Write a property on a QML item or singleton (e.g. switch theme, jump to a page).

    Replaces any binding on that property. Grouped names like
    "font.pixelSize" work. Returns the previous and new value.

    Args:
        item: objectName of the item, or a singleton identifier.
        property: Property name.
        value: New value (JSON number, string, bool, list or object).
    """
    app, rec = _ctx_app(ctx)
    if rec:
        return await rec.set(app, item, property, value)
    return await app.set(item, property, value)


@mcp.tool()
async def chuck_invoke(
    ctx,
    item: str,
    method: str,
    args: list[Any] | None = None,
) -> Any:
    """Call a QML function, Q_INVOKABLE, slot or signal on an item or singleton.

    Args:
        item: objectName of the item, or a singleton identifier.
        method: Method name.
        args: Positional arguments.
    """
    app, rec = _ctx_app(ctx)
    if rec:
        return await rec.invoke(app, item, method, *(args or []))
    return await app.invoke(item, method, *(args or []))


@mcp.tool(structured_output=False)
async def chuck_screenshot(
    ctx,
    item: str | None = None,
    tag: str | None = None,
    verify: bool = False,
    method: str = "ssim",
    threshold: float = 0.95,
) -> Image | list[Image | str]:
    """Capture a screenshot of the application window (or a specific item).

    Returns the screenshot as an image that the AI can see and analyze.
    With *tag*, the first capture is saved as that tag's baseline; with
    *verify*, later captures are compared to it and the comparison result
    (match, score, message) is returned as JSON text next to the image.

    Args:
        item: objectName of a specific item to capture. Omit for full window.
        tag: Label for baseline management.
        verify: Compare against the stored baseline for *tag*.
        method: Comparison method ("ssim", "phash" or "exact").
        threshold: Minimum similarity score for a match.
    """
    app, rec = _ctx_app(ctx)

    result = await app.raw.call("screenshot", item=item)
    if rec:
        rec._write("screenshot", item=item, tag=tag, verify=verify or None)

    if "data" not in result:
        # Return a placeholder if no image data
        return Image(data=b"", format="png")

    png_bytes = base64.b64decode(result["data"])
    image = Image(data=png_bytes, format="png")
    if not tag:
        return image

    cmp = app.check_baseline(png_bytes, tag, verify=verify, method=method, threshold=threshold)
    if not cmp:
        return image
    return [image, json.dumps(cmp)]


@mcp.tool()
async def chuck_wait(
    ctx,
    item: str,
    property: str | None = None,
    value: Any = None,
    timeout: int = 5000,
) -> dict[str, Any]:
    """Wait for a QML item to appear or a property to reach a specific value.

    Args:
        item: objectName of the item to wait for.
        property: Property name to watch.
        value: Expected property value.
        timeout: Maximum wait time in milliseconds.
    """
    app, rec = _ctx_app(ctx)
    if rec:
        return await rec.wait(app, item, property=property, value=value, timeout=timeout)
    return await app.wait(item, property=property, value=value, timeout=timeout)


@mcp.tool()
async def chuck_tree(
    ctx,
    root: str | None = None,
    depth: int | None = None,
    props: str | None = None,
) -> list[dict[str, Any]]:
    """Dump the QML object tree as a flat list.

    Useful for discovering what items exist in the application.

    Args:
        root: objectName of the root item to start from.
        depth: Maximum tree depth.
        props: Comma-separated property names to include per item.
    """
    app, rec = _ctx_app(ctx)
    # tree is not directly on Recorder, use find/get pattern
    # Recorder doesn't have a tree wrapper, so we record as find + call tree directly
    result = await app.tree(root=root, depth=depth, props=props)
    if rec:
        rec._write("tree", root=root, depth=depth, props=props)
    return result


@mcp.tool()
async def chuck_item_at(
    ctx,
    x: float,
    y: float,
    props: str | None = None,
) -> dict[str, Any]:
    """Find what is drawn at a point in the window, like an inspector's pick mode.

    Returns the topmost visible item with its "source" (QML file:line:column
    where it is declared) and its "ancestors", nearest first. The hit is often
    an unnamed internal (a control's background or label); the first named
    ancestor and its source line are usually what you want to edit.

    Args:
        x: Window x coordinate (same space as screenshots and click points).
        y: Window y coordinate.
        props: Properties to include for the hit item ("minimal" for none).
    """
    app, _ = _ctx_app(ctx)
    return await app.item_at(x, y, props=props)


@mcp.tool()
async def chuck_profile_start(
    ctx,
    qml_port: int,
    features: str | None = None,
) -> dict[str, Any]:
    """Start recording a QML profiler trace.

    The app must have been launched with
    -qmljsdebugger=port:<qml_port>,host:127.0.0.1,services:CanvasFrameRate,EngineControl,DebugMessages
    (and call tester::enableQmlDebuggingFromArguments). Drive the scenario,
    then call chuck_profile_stop. Needs qmlprofiler from Qt's bin directory
    on PATH or in CHUCK_QT_BIN.

    Args:
        qml_port: The app's QML debug port.
        features: qmlprofiler features, comma separated. Defaults to QML
            compiling/creating/binding/javascript/signals/pixmaps; add
            "scenegraph,animations,memory" for rendering problems.
    """
    lc: AppContext = ctx.request_context.lifespan_context
    if lc.profiler is None:
        selected = features.split(",") if features else DEFAULT_PROFILE_FEATURES
        lc.profiler = QmlProfiler(qml_port, features=selected)
        lc.profiler.attach()
    lc.profiler.start()
    return {"recording": True, "qml_port": qml_port}


@mcp.tool()
async def chuck_profile_stop(
    ctx,
    output: str,
    start_s: float = 0.0,
    end_s: float | None = None,
) -> str:
    """Stop recording, save the trace to a .qtd file and return a summary.

    The summary lists self time by event type, the costliest events with
    their QML file:line, and long top-level ranges. Call again with
    chuck_profile_start to record another scenario on the same connection.

    Args:
        output: Path of the .qtd file to write.
        start_s: Summarise only from this many seconds into the trace.
        end_s: Summarise only up to this many seconds into the trace.
    """
    lc: AppContext = ctx.request_context.lifespan_context
    if lc.profiler is None:
        raise RuntimeError("Not profiling: call chuck_profile_start first")
    path = lc.profiler.save(output)
    return f"Wrote {path}\n" + summarize(path, start_s=start_s, end_s=end_s).format()


@mcp.tool()
async def chuck_quit(ctx) -> dict[str, Any]:
    """Ask the application to quit gracefully."""
    app, rec = _ctx_app(ctx)
    if rec:
        return await rec.quit(app)
    return await app.quit()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def run_server() -> None:
    """Start the MCP server with stdio transport."""
    mcp.run(transport="stdio")
