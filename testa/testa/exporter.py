"""Convert a JSONL session file to a Python test script."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


def export_to_python(session_path: str | Path, output_path: str | Path) -> None:
    """Read a .jsonl session and write a pytest-compatible Python test."""
    session = Path(session_path)
    output = Path(output_path)

    steps = _load_steps(session)
    code = _generate_python(steps, session.stem)

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(code, encoding="utf-8")
    print(f"Exported {len(steps)} steps → {output}")


def _load_steps(path: Path) -> list[dict[str, Any]]:
    steps: list[dict[str, Any]] = []
    with path.open(encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                steps.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    return steps


def _generate_python(steps: list[dict[str, Any]], name: str) -> str:
    lines = [
        f"# Auto-generated from {name}.jsonl",
        "import pytest",
        "from testa import TestaClient",
        "",
        "",
        "@pytest.mark.asyncio",
        f"async def test_{_safe_name(name)}():",
        "    async with TestaClient(port=1408) as app:",
    ]

    for step in steps:
        action = step.get("action", "")
        step.pop("ts", None)
        step.pop("action", None)
        tag = step.pop("tag", None)
        verify = step.pop("verify", False)

        line = _step_to_python(action, step, tag, verify)
        if line:
            lines.append(f"        {line}")

    lines.append("")
    return "\n".join(lines)


def _step_to_python(action: str, params: dict[str, Any], tag: str | None, verify: bool) -> str:
    match action:
        case "click":
            item = _format_item(params.get("item"))
            extras = _format_kwargs(params, exclude={"item"})
            return f"await app.click({item}{extras})"

        case "drag":
            from_ = _format_item(params.get("from"))
            to_ = _format_item(params.get("to"))
            return f"await app.drag({from_}, {to_})"

        case "swipe":
            item = _format_item(params.get("item"))
            extras = _format_kwargs(params, exclude={"item"})
            return f"await app.swipe({item}{extras})"

        case "keys":
            item = _format_item(params.get("item"))
            extras = _format_kwargs(params, exclude={"item"})
            return f"await app.keys({item}{extras})"

        case "screenshot":
            parts: list[str] = []
            if params.get("item"):
                parts.append(_format_item(params["item"]))
            if tag:
                parts.append(f'save_to="screenshots/{tag}.png"')
            return f"await app.screenshot({', '.join(parts)})"

        case "wait":
            item = _format_item(params.get("item"))
            extras = _format_kwargs(params, exclude={"item"})
            return f"await app.wait({item}{extras})"

        case "find":
            extras = _format_kwargs(params)
            return f"await app.find({extras.lstrip(', ')})"

        case "get":
            item = _format_item(params.get("item"))
            extras = _format_kwargs(params, exclude={"item"})
            return f"await app.get({item}{extras})"

        case "tree":
            extras = _format_kwargs(params)
            return f"await app.tree({extras.lstrip(', ')})"

        case "quit":
            return "await app.quit()"

        case _:
            return f"# unknown action: {action}"


def _format_item(item: Any) -> str:
    if isinstance(item, str):
        return repr(item)
    if isinstance(item, dict):
        return repr(item)
    return "None"


def _format_kwargs(params: dict[str, Any], *, exclude: set[str] | None = None) -> str:
    exclude = exclude or set()
    parts: list[str] = []
    for k, v in params.items():
        if k in exclude or v is None:
            continue
        # Map Python-unfriendly keys
        py_key = k.replace("-", "_")
        if py_key == "from":
            py_key = "from_item"
        parts.append(f"{py_key}={v!r}")
    if not parts:
        return ""
    return ", " + ", ".join(parts)


def _safe_name(name: str) -> str:
    return "".join(c if c.isalnum() or c == "_" else "_" for c in name)
