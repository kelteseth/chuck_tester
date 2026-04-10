"""High-level async API for driving a chuck-enabled Qt/QML application."""

from __future__ import annotations

import base64
from pathlib import Path
from typing import Any

from .client import Client, ChuckError


class TestaClient:
    """Ergonomic wrapper around the low-level JSON-RPC client.

    Usage::

        async with TestaClient(port=9223) as app:
            await app.click("saveBtn")
            await app.keys("nameField", text="hello")
            img = await app.screenshot()
    """

    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 9223,
        *,
        uri: str | None = None,
    ) -> None:
        self._uri = uri or f"ws://{host}:{port}"
        self._client = Client(self._uri)

    async def __aenter__(self) -> TestaClient:
        await self._client.connect()
        return self

    async def __aexit__(self, *exc: object) -> None:
        await self._client.close()

    @property
    def raw(self) -> Client:
        """Access the underlying JSON-RPC client for advanced use."""
        return self._client

    # -- commands matching the chuck method table --

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
        save_to: str | Path | None = None,
    ) -> dict[str, Any]:
        """Capture a screenshot. If *save_to* is given, decode and save the PNG."""
        result = await self._client.call("screenshot", item=item)
        if save_to and "data" in result:
            path = Path(save_to)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(base64.b64decode(result["data"]))
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
