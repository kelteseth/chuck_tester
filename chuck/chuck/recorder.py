"""Records tester commands to a JSONL file as they are executed."""

from __future__ import annotations

import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


class Recorder:
    """Wraps a ChuckClient to record every command to a JSONL file.

    Usage::

        async with ChuckClient(port=1408) as app:
            rec = Recorder("session.jsonl")
            rec.start()

            # Every call records a line
            await rec.click(app, "saveBtn")
            await rec.keys(app, "nameField", text="hello")

            rec.stop()
    """

    def __init__(self, path: str | Path) -> None:
        self._path = Path(path)
        self._file = None

    def start(self) -> None:
        self._path.parent.mkdir(parents=True, exist_ok=True)
        self._file = self._path.open("a", encoding="utf-8")

    def stop(self) -> None:
        if self._file:
            self._file.close()
            self._file = None

    def _write(self, action: str, **fields: Any) -> None:
        if not self._file:
            return
        entry: dict[str, Any] = {
            "ts": datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
            "action": action,
        }
        for k, v in fields.items():
            if v is not None:
                entry[k] = v
        self._file.write(json.dumps(entry, ensure_ascii=False) + "\n")
        self._file.flush()

    # -- recording wrappers (mirror ChuckClient methods) --

    async def click(self, app, item, *, button=None, double=False, hold=None, root=None):
        result = await app.click(item, button=button, double=double, hold=hold, root=root)
        self._write("click", item=item, button=button, double=double or None, hold=hold)
        return result

    async def drag(self, app, from_item, to_item, *, root=None):
        result = await app.drag(from_item, to_item, root=root)
        self._write("drag", **{"from": from_item, "to": to_item})
        return result

    async def swipe(self, app, item, *, direction="up", distance=200):
        result = await app.swipe(item, direction=direction, distance=distance)
        self._write("swipe", item=item, direction=direction, distance=distance)
        return result

    async def keys(self, app, item=None, *, text=None, key=None, shortcut=None, root=None):
        result = await app.keys(item, text=text, key=key, shortcut=shortcut, root=root)
        self._write("keys", item=item, text=text, key=key, shortcut=shortcut)
        return result

    async def screenshot(self, app, item=None, *, tag=None, verify=False, save_to=None):
        result = await app.screenshot(item, save_to=save_to)
        self._write("screenshot", item=item, tag=tag, verify=verify or None)
        return result

    async def wait(self, app, item, *, property=None, value=None, timeout=5000):
        result = await app.wait(item, property=property, value=value, timeout=timeout)
        self._write("wait", item=item, property=property, value=value, timeout=timeout)
        return result

    async def find(self, app, **kwargs):
        result = await app.find(**kwargs)
        self._write("find", **kwargs)
        return result

    async def get(self, app, item, *, props=None):
        result = await app.get(item, props=props)
        self._write("get", item=item, props=props)
        return result

    async def quit(self, app):
        result = await app.quit()
        self._write("quit")
        return result
