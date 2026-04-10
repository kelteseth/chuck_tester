"""Low-level WebSocket client speaking JSON-RPC 2.0 to a chuck server."""

from __future__ import annotations

import asyncio
import json
import itertools
from typing import Any

import websockets.asyncio.client as ws_client
import websockets.exceptions


class ChuckError(Exception):
    """Raised when the chuck server returns a JSON-RPC 2.0 error response."""

    def __init__(self, code: int, message: str, data: Any = None) -> None:
        self.code = code
        self.message = message
        self.data = data
        super().__init__(f"[{code}] {message}")


class Client:
    """Async WebSocket client for the chuck automation server.

    Usage::

        async with Client("ws://127.0.0.1:1408") as c:
            result = await c.call("click", item="myButton")
    """

    def __init__(self, uri: str = "ws://127.0.0.1:1408") -> None:
        self._uri = uri
        self._ws: ws_client.ClientConnection | None = None
        self._id_gen = itertools.count(1)
        self._pending: dict[int, asyncio.Future[Any]] = {}
        self._reader_task: asyncio.Task[None] | None = None

    # -- context manager --

    async def connect(self) -> None:
        self._ws = await ws_client.connect(self._uri)
        self._reader_task = asyncio.create_task(self._reader_loop())

    async def close(self) -> None:
        if self._reader_task:
            self._reader_task.cancel()
            try:
                await self._reader_task
            except asyncio.CancelledError:
                pass
            self._reader_task = None
        if self._ws:
            await self._ws.close()
            self._ws = None
        for fut in self._pending.values():
            if not fut.done():
                fut.cancel()
        self._pending.clear()

    async def __aenter__(self) -> Client:
        await self.connect()
        return self

    async def __aexit__(self, *exc: object) -> None:
        await self.close()

    # -- public API --

    async def call(self, method: str, **params: Any) -> Any:
        """Send a JSON-RPC 2.0 request and return the result.

        Raises ChuckError on error responses.
        """
        if not self._ws:
            raise RuntimeError("Not connected")

        msg_id = next(self._id_gen)
        request = {
            "jsonrpc": "2.0",
            "id": msg_id,
            "method": method,
            "params": {k: v for k, v in params.items() if v is not None},
        }

        loop = asyncio.get_running_loop()
        future: asyncio.Future[Any] = loop.create_future()
        self._pending[msg_id] = future

        await self._ws.send(json.dumps(request))
        return await future

    async def notify(self, method: str, **params: Any) -> None:
        """Send a JSON-RPC 2.0 notification (no id, no response expected)."""
        if not self._ws:
            raise RuntimeError("Not connected")

        request = {
            "jsonrpc": "2.0",
            "method": method,
            "params": {k: v for k, v in params.items() if v is not None},
        }
        await self._ws.send(json.dumps(request))

    # -- internal --

    async def _reader_loop(self) -> None:
        assert self._ws is not None
        try:
            async for raw in self._ws:
                try:
                    msg = json.loads(raw)
                except json.JSONDecodeError:
                    continue

                msg_id = msg.get("id")
                if msg_id is None:
                    continue

                future = self._pending.pop(msg_id, None)
                if future is None or future.done():
                    continue

                if "error" in msg:
                    err = msg["error"]
                    future.set_exception(
                        ChuckError(err.get("code", -1), err.get("message", ""), err.get("data"))
                    )
                else:
                    future.set_result(msg.get("result"))
        except websockets.exceptions.ConnectionClosed:
            pass
        except asyncio.CancelledError:
            raise
        finally:
            for fut in self._pending.values():
                if not fut.done():
                    fut.set_exception(ConnectionError("WebSocket connection closed"))
            self._pending.clear()
