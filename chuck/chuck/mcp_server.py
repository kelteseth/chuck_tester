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
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager
from dataclasses import dataclass, field
from typing import Any

from mcp.server.fastmcp import FastMCP, Image

from .commands import ChuckClient
from .recorder import Recorder


# ---------------------------------------------------------------------------
# Lifespan — connect/disconnect the ChuckClient
# ---------------------------------------------------------------------------

@dataclass
class AppContext:
    """Holds the live ChuckClient (and optional recorder) for MCP tools."""

    app: ChuckClient
    recorder: Recorder | None = None


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

    try:
        yield AppContext(app=app, recorder=recorder)
    finally:
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
async def chuck_screenshot(
    ctx,
    item: str | None = None,
    tag: str | None = None,
) -> Image:
    """Capture a screenshot of the application window (or a specific item).

    Returns the screenshot as an image that the AI can see and analyze.

    Args:
        item: objectName of a specific item to capture. Omit for full window.
        tag: Optional label for baseline management.
    """
    app, rec = _ctx_app(ctx)

    # Call through recorder if active (for JSONL logging)
    if rec:
        await rec.screenshot(app, item, tag=tag)

    # Get the raw screenshot with base64 data
    result = await app._client.call("screenshot", item=item)

    if "data" not in result:
        # Return a placeholder if no image data
        return Image(data=b"", format="png")

    png_bytes = base64.b64decode(result["data"])
    return Image(data=png_bytes, format="png")


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


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def run_server() -> None:
    """Start the MCP server with stdio transport."""
    mcp.run(transport="stdio")
