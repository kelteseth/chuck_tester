"""High-level async API for driving a tester-enabled Qt/QML application."""

from __future__ import annotations

import base64
import io
import warnings
from pathlib import Path
from typing import Any

from PIL import Image

from .client import Client, ChuckError
from .screenshots import BaselineStore
from .visual_diff import CompareResult

# Protocol this chuck was written against; see ProtocolVersion in
# tester/src/CommandHandler.h for what each number added.
PROTOCOL_VERSION = 2

_ERR_METHOD_NOT_FOUND = -32601

# What a tester from before the "version" handshake (protocol 1) understands.
_PROTOCOL_1_METHODS = frozenset({
    "find", "get", "click", "drag", "swipe", "keys", "screenshot", "tree", "wait", "quit",
})


class ChuckClient:
    """Ergonomic wrapper around the low-level JSON-RPC client.

    Usage::

        async with ChuckClient(port=1408) as app:
            await app.click("saveBtn")
            await app.keys("nameField", text="hello")
            img = await app.screenshot()
    """

    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 1408,
        *,
        uri: str | None = None,
        baseline_dir: str | Path = "baselines",
    ) -> None:
        self._uri = uri or f"ws://{host}:{port}"
        self._client = Client(self._uri)
        self._baselines = BaselineStore(baseline_dir)
        self._server: dict[str, Any] = {}

    async def __aenter__(self) -> ChuckClient:
        await self._client.connect()
        await self._handshake()
        return self

    async def _handshake(self) -> None:
        try:
            self._server = await self._client.call("version")
        except ChuckError as e:
            if e.code != _ERR_METHOD_NOT_FOUND:
                raise
            self._server = {"protocol": 1, "methods": sorted(_PROTOCOL_1_METHODS)}

        protocol = self._server.get("protocol", 1)
        if protocol < PROTOCOL_VERSION:
            warnings.warn(
                f"The app's tester speaks protocol {protocol}, this chuck speaks {PROTOCOL_VERSION}. "
                "Newer commands will be refused; rebuild the app against the current tester.",
                stacklevel=3,
            )
        elif protocol > PROTOCOL_VERSION:
            warnings.warn(
                f"The app's tester speaks protocol {protocol}, newer than this chuck ({PROTOCOL_VERSION}). "
                "Update chuck to use its newer commands.",
                stacklevel=3,
            )

    @property
    def server(self) -> dict[str, Any]:
        """What the app's tester reported on connect: protocol, tester and Qt version, methods."""
        return self._server

    async def _call(self, method: str, /, **params: Any) -> Any:
        methods = self._server.get("methods")
        if methods is not None and method not in methods:
            if method in ("set", "invoke") and self._server.get("scripting") is False:
                reason = "scripting is disabled in the app (TesterServer::Config::allowScripting)"
            else:
                reason = (
                    f"the app's tester (protocol {self._server.get('protocol', 1)}) doesn't support it - "
                    "rebuild the app against a newer tester"
                )
            raise ChuckError(_ERR_METHOD_NOT_FOUND, f"Cannot call '{method}': {reason}")
        return await self._client.call(method, **params)

    async def __aexit__(self, *exc: object) -> None:
        await self._client.close()

    @property
    def raw(self) -> Client:
        """Access the underlying JSON-RPC client for advanced use."""
        return self._client

    @property
    def baselines(self) -> BaselineStore:
        """Access the baseline screenshot store."""
        return self._baselines

    # -- commands matching the tester method table --

    async def find(
        self,
        *,
        type: str | None = None,
        interactive: bool = False,
        accessible: bool = False,
        depth: int | None = None,
        root: str | dict[str, Any] | None = None,
        props: str | None = None,
    ) -> list[dict[str, Any]]:
        """Search the QML tree for items."""
        result = await self._call(
            "find",
            type=type,
            interactive=interactive or None,
            accessible=accessible or None,
            depth=depth,
            root=root,
            props=props,
        )
        return result.get("items", []) if isinstance(result, dict) else result

    async def get(
        self,
        item: str | dict[str, Any],
        *,
        props: str | None = None,
    ) -> dict[str, Any]:
        """Read properties of a specific item."""
        return await self._call("get", item=item, props=props)

    async def click(
        self,
        item: str | dict[str, Any],
        *,
        button: str | None = None,
        double: bool = False,
        hold: int | None = None,
        root: str | dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        """Click an item."""
        return await self._call(
            "click",
            item=item,
            button=button,
            double=double or None,
            hold=hold,
            root=root,
        )

    async def drag(
        self,
        from_item: str | dict[str, Any],
        to_item: str | dict[str, Any] | None = None,
        *,
        by: tuple[int, int] | list[int] | None = None,
        steps: int | None = None,
        root: str | dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        """Drag from one item to another item, or by a pixel offset.

        Exactly one of *to_item* or *by* must be given. *by* is a (dx, dy)
        offset from the source item's center - the natural form for slider
        and timeline-handle drags where nothing sits at the drop position.

        The gesture is synthesized as press -> *steps* incremental moves ->
        release so QML DragHandler/gesture recognizers engage properly.
        """
        if (to_item is None) == (by is None):
            raise ValueError("drag() needs exactly one of to_item or by")
        params: dict[str, Any] = {"from": from_item, "root": root, "steps": steps}
        if to_item is not None:
            params["to"] = to_item
        if by is not None:
            params["by"] = list(by)
        return await self._call("drag", **params)

    async def swipe(
        self,
        item: str | dict[str, Any],
        *,
        direction: str = "up",
        distance: int = 200,
    ) -> dict[str, Any]:
        """Swipe/scroll gesture on an item."""
        return await self._call(
            "swipe",
            item=item,
            direction=direction,
            distance=distance,
        )

    async def hover(
        self,
        item: str | dict[str, Any] | None = None,
        *,
        by: tuple[int, int] | list[int] | None = None,
        point: tuple[int, int] | list[int] | None = None,
        root: str | dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        """Move the mouse (no button held) over an item, or to a window point.

        Exactly one of *item* or *point* must be given. *by* offsets the
        item's center. Hover the window background (or another item) to end
        a hover - there is no separate "unhover".
        """
        if (item is None) == (point is None):
            raise ValueError("hover() needs exactly one of item or point")
        return await self._call(
            "hover",
            item=item,
            by=list(by) if by is not None else None,
            point=list(point) if point is not None else None,
            root=root,
        )

    async def scroll(
        self,
        item: str | dict[str, Any],
        *,
        direction: str = "down",
        steps: int = 1,
        modifiers: str | None = None,
        root: str | dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        """Turn the mouse wheel over an item, *steps* notches in *direction*.

        *modifiers* is e.g. ``"Ctrl"`` or ``"Ctrl+Shift"`` (for zoom-on-wheel).
        Flickables animate wheel scrolling, so wait for ``contentY`` (or
        ``moving == False``) before asserting on the position.
        """
        return await self._call(
            "scroll",
            item=item,
            direction=direction,
            steps=steps,
            modifiers=modifiers,
            root=root,
        )

    async def window(
        self,
        *,
        state: str | None = None,
        frameless: bool | None = None,
        stay_on_top: bool | None = None,
        width: int | None = None,
        height: int | None = None,
        x: int | None = None,
        y: int | None = None,
    ) -> dict[str, Any]:
        """Change the window's state, flags or geometry. Call with no arguments to just read them.

        *state* is ``"normal"``, ``"maximized"``, ``"minimized"`` or
        ``"fullscreen"``. Size/position imply (and only combine with) the
        normal state. Returns the window's actual state, flags and geometry
        afterwards - the window manager may clamp or delay a request, so
        compare against the result rather than the request.
        """
        return await self._call(
            "window",
            state=state,
            frameless=frameless,
            stayOnTop=stay_on_top,
            width=width,
            height=height,
            x=x,
            y=y,
        )

    async def resize(self, width: int, height: int) -> dict[str, Any]:
        """Resize the window (restoring it from maximized/fullscreen first)."""
        return await self.window(width=width, height=height)

    async def set(
        self,
        item: str | dict[str, Any],
        property: str,
        value: Any,
    ) -> dict[str, Any]:
        """Write a property on an item or singleton.

        Grouped names like ``"font.pixelSize"`` work. Writing replaces any
        binding on that property, just as an assignment in QML would.
        """
        return await self._call("set", item=item, property=property, value=value)

    async def invoke(
        self,
        item: str | dict[str, Any],
        method: str,
        *args: Any,
    ) -> Any:
        """Call a QML function, Q_INVOKABLE, slot or signal and return its result."""
        result = await self._call("invoke", item=item, method=method, args=list(args))
        return result.get("result")

    async def keys(
        self,
        item: str | dict[str, Any] | None = None,
        *,
        text: str | None = None,
        key: str | None = None,
        shortcut: str | None = None,
        root: str | dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        """Type text, send a special key, or a keyboard shortcut."""
        return await self._call(
            "keys",
            item=item,
            text=text,
            key=key,
            shortcut=shortcut,
            root=root,
        )

    async def screenshot(
        self,
        item: str | dict[str, Any] | None = None,
        *,
        tag: str | None = None,
        verify: bool = False,
        save_to: str | Path | None = None,
        method: str = "ssim",
        threshold: float = 0.95,
        mask_regions: list[tuple[int, int, int, int]] | None = None,
    ) -> dict[str, Any]:
        """Capture a screenshot, optionally saving and/or verifying against a baseline.

        Args:
            item: Optional item to crop to.
            tag: Label for baseline management. Required if *verify* is True.
            verify: If True, compare against stored baseline for *tag*.
            save_to: Explicit path to save the PNG to.
            method: Comparison method ('ssim', 'phash', 'exact').
            threshold: Minimum similarity score for a match.
            mask_regions: Regions to ignore during comparison.

        Returns:
            dict with 'width', 'height', and optionally 'match', 'score' keys.
        """
        result = await self._call("screenshot", item=item)

        if "data" not in result:
            return result

        png_bytes = base64.b64decode(result["data"])

        if save_to:
            path = Path(save_to)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(png_bytes)

        if tag:
            result.update(self.check_baseline(
                png_bytes, tag,
                verify=verify, method=method, threshold=threshold, mask_regions=mask_regions,
            ))

        # Strip the large base64 payload from the return dict
        result.pop("data", None)
        return result

    def check_baseline(
        self,
        png_bytes: bytes,
        tag: str,
        *,
        verify: bool = False,
        method: str = "ssim",
        threshold: float = 0.95,
        mask_regions: list[tuple[int, int, int, int]] | None = None,
    ) -> dict[str, Any]:
        """Compare a PNG against the baseline for *tag*, creating it if missing.

        Returns the 'match'/'score'/'method'/'message' or 'baseline_created'
        keys that :meth:`screenshot` merges into its result.
        """
        actual = Image.open(io.BytesIO(png_bytes))
        if not self._baselines.has_baseline(tag):
            # No baseline yet — save current as the baseline
            self._baselines.save_baseline(tag, actual)
            return {"baseline_created": True}
        if not verify:
            return {}
        cmp = self._baselines.verify(
            tag,
            actual,
            method=method,
            threshold=threshold,
            mask_regions=mask_regions,
        )
        out: dict[str, Any] = {"match": cmp.match, "score": cmp.score, "method": cmp.method}
        if not cmp.match:
            out["message"] = cmp.message
        return out

    async def tree(
        self,
        *,
        root: str | dict[str, Any] | None = None,
        depth: int | None = None,
        props: str | None = None,
    ) -> list[dict[str, Any]]:
        """Dump the QML tree as a list of items."""
        result = await self._call("tree", root=root, depth=depth, props=props)
        return result.get("items", []) if isinstance(result, dict) else result

    async def wait(
        self,
        item: str | dict[str, Any],
        *,
        property: str | None = None,
        value: Any = None,
        timeout: int = 5000,
    ) -> dict[str, Any]:
        """Wait for an item to appear or a property to reach a value."""
        return await self._call(
            "wait",
            item=item,
            property=property,
            value=value,
            timeout=timeout,
        )

    async def quit(self) -> dict[str, Any]:
        """Ask the AUT to quit gracefully."""
        return await self._call("quit")
