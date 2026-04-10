"""Replay a JSONL session file by sending each step to the AUT."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

from .client import ChuckError
from .commands import TestaClient


async def replay(
    session_path: str | Path,
    *,
    host: str = "127.0.0.1",
    port: int = 9223,
) -> bool:
    """Replay a .jsonl session file. Returns True if all steps passed."""
    path = Path(session_path)
    steps = _load_steps(path)
    print(f"Replaying {len(steps)} steps from {path.name}")

    ok = True
    async with TestaClient(host=host, port=port) as app:
        for i, step in enumerate(steps, 1):
            action = step.pop("action")
            step.pop("ts", None)  # timing is informational
            tag = step.pop("tag", None)
            verify = step.pop("verify", False)

            label = f"[{i}/{len(steps)}] {action}"
            if tag:
                label += f" ({tag})"

            try:
                result = await _dispatch(app, action, step)
                print(f"  {label} ✓")
            except ChuckError as e:
                print(f"  {label} ✗ {e}", file=sys.stderr)
                ok = False
                break
            except Exception as e:
                print(f"  {label} ✗ {type(e).__name__}: {e}", file=sys.stderr)
                ok = False
                break

    status = "PASSED" if ok else "FAILED"
    print(f"\nReplay {status}")
    return ok


def _load_steps(path: Path) -> list[dict[str, Any]]:
    steps: list[dict[str, Any]] = []
    with path.open(encoding="utf-8") as f:
        for line_no, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                steps.append(json.loads(line))
            except json.JSONDecodeError as e:
                print(f"Warning: skipping invalid JSON at line {line_no}: {e}", file=sys.stderr)
    return steps


async def _dispatch(app: TestaClient, action: str, params: dict[str, Any]) -> Any:
    match action:
        case "click":
            return await app.click(params.pop("item"), **params)
        case "drag":
            return await app.drag(params.pop("from"), params.pop("to"), **params)
        case "swipe":
            return await app.swipe(params.pop("item"), **params)
        case "keys":
            return await app.keys(params.pop("item", None), **params)
        case "screenshot":
            return await app.screenshot(params.pop("item", None), **params)
        case "wait":
            return await app.wait(params.pop("item"), **params)
        case "find":
            return await app.find(**params)
        case "get":
            return await app.get(params.pop("item"), **params)
        case "tree":
            return await app.tree(**params)
        case "quit":
            return await app.quit()
        case _:
            raise ValueError(f"Unknown action: {action}")
