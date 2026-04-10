"""High-level async API for driving a tester-enabled Qt/QML application."""

from __future__ import annotations

import base64
import io
from pathlib import Path
from typing import Any

from PIL import Image

from .client import Client, ChuckError
from .screenshots import BaselineStore
from .visual_diff import CompareResult


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

    async def __aenter__(self) -> ChuckClient:
        await self._client.connect()
        return self

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
        result = await self._client.call(
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
        return await self._client.call("get", item=item, props=props)

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
        return await self._client.call(
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
        to_item: str | dict[str, Any],
        *,
        root: str | dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        """Drag from one item to another."""
        # 'from' is a Python keyword, so we use from_item/to_item and map them
        return await self._client.call("drag", **{"from": from_item, "to": to_item, "root": root})

    async def swipe(
        self,
        item: str | dict[str, Any],
        *,
        direction: str = "up",
        distance: int = 200,
    ) -> dict[str, Any]:
        """Swipe/scroll gesture on an item."""
        return await self._client.call(
            "swipe",
            item=item,
            direction=direction,
            distance=distance,
        )

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
        return await self._client.call(
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
        result = await self._client.call("screenshot", item=item)

        if "data" not in result:
            return result

        png_bytes = base64.b64decode(result["data"])

        if save_to:
            path = Path(save_to)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(png_bytes)

        if tag:
            actual = Image.open(io.BytesIO(png_bytes))

            if verify and self._baselines.has_baseline(tag):
                cmp = self._baselines.verify(
                    tag,
                    actual,
                    method=method,
                    threshold=threshold,
                    mask_regions=mask_regions,
                )
                result["match"] = cmp.match
                result["score"] = cmp.score
                result["method"] = cmp.method
                if not cmp.match:
                    result["message"] = cmp.message
            elif not self._baselines.has_baseline(tag):
                # No baseline yet — save current as the baseline
                self._baselines.save_baseline(tag, actual)
                result["baseline_created"] = True

        # Strip the large base64 payload from the return dict
        result.pop("data", None)
        return result

    async def tree(
        self,
        *,
        root: str | dict[str, Any] | None = None,
        depth: int | None = None,
        props: str | None = None,
    ) -> list[dict[str, Any]]:
        """Dump the QML tree as a list of items."""
        result = await self._client.call("tree", root=root, depth=depth, props=props)
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
        return await self._client.call(
            "wait",
            item=item,
            property=property,
            value=value,
            timeout=timeout,
        )

    async def quit(self) -> dict[str, Any]:
        """Ask the AUT to quit gracefully."""
        return await self._client.call("quit")
