"""Interactive REPL for manual testing against a chuck-enabled AUT."""

from __future__ import annotations

import ast
import json
import shlex
import sys
import traceback

from .commands import TestaClient


async def run_repl(*, host: str = "127.0.0.1", port: int = 9223) -> None:
    """Start an interactive REPL connected to a chuck server."""
    print(f"Connecting to ws://{host}:{port} ...")

    async with TestaClient(host=host, port=port) as app:
        print("Connected. Type commands like: click saveBtn")
        print("Type 'help' for available commands, 'quit' to exit.\n")

        while True:
            try:
                line = input("testa> ").strip()
            except (EOFError, KeyboardInterrupt):
                print()
                break

            if not line:
                continue
            if line in ("quit", "exit", "q"):
                break
            if line == "help":
                _print_help()
                continue

            try:
                result = await _execute(app, line)
                print(json.dumps(result, indent=2, default=str))
            except Exception as e:
                print(f"Error: {e}", file=sys.stderr)


async def _execute(app: TestaClient, line: str) -> object:
    parts = shlex.split(line)
    cmd = parts[0]
    args = parts[1:]

    match cmd:
        case "find":
            kwargs = _parse_kwargs(args)
            return await app.find(**kwargs)

        case "get":
            if not args:
                raise ValueError("Usage: get <item> [props=...]")
            item = _parse_item(args[0])
            kwargs = _parse_kwargs(args[1:])
            return await app.get(item, **kwargs)

        case "click":
            if not args:
                raise ValueError("Usage: click <item>")
            item = _parse_item(args[0])
            kwargs = _parse_kwargs(args[1:])
            return await app.click(item, **kwargs)

        case "drag":
            if len(args) < 2:
                raise ValueError("Usage: drag <from> <to>")
            return await app.drag(_parse_item(args[0]), _parse_item(args[1]))

        case "swipe":
            if not args:
                raise ValueError("Usage: swipe <item> [direction=up] [distance=200]")
            item = _parse_item(args[0])
            kwargs = _parse_kwargs(args[1:])
            return await app.swipe(item, **kwargs)

        case "keys":
            item = _parse_item(args[0]) if args else None
            kwargs = _parse_kwargs(args[1:] if args else [])
            return await app.keys(item, **kwargs)

        case "screenshot":
            item = _parse_item(args[0]) if args else None
            kwargs = _parse_kwargs(args[1:] if args else [])
            result = await app.screenshot(item, **kwargs)
            # Don't dump the base64 blob to console
            if "data" in result:
                result = {**result, "data": f"<{len(result['data'])} chars base64>"}
            return result

        case "tree":
            kwargs = _parse_kwargs(args)
            return await app.tree(**kwargs)

        case "wait":
            if not args:
                raise ValueError("Usage: wait <item> [property=...] [value=...]")
            item = _parse_item(args[0])
            kwargs = _parse_kwargs(args[1:])
            return await app.wait(item, **kwargs)

        case "raw":
            # raw method key=value key=value
            if not args:
                raise ValueError("Usage: raw <method> [key=value ...]")
            method = args[0]
            kwargs = _parse_kwargs(args[1:])
            return await app.raw.call(method, **kwargs)

        case _:
            raise ValueError(f"Unknown command: {cmd}. Type 'help' for available commands.")


def _parse_item(s: str) -> str | dict:
    """Parse an item argument — JSON object or plain string."""
    if s.startswith("{"):
        try:
            return json.loads(s)
        except json.JSONDecodeError:
            pass
    return s


def _parse_kwargs(args: list[str]) -> dict:
    """Parse key=value pairs from command-line-style arguments."""
    kwargs: dict = {}
    for arg in args:
        if "=" not in arg:
            continue
        key, val = arg.split("=", 1)
        # Try to parse as Python literal (int, bool, etc.)
        try:
            kwargs[key] = ast.literal_eval(val)
        except (ValueError, SyntaxError):
            kwargs[key] = val
    return kwargs


def _print_help() -> None:
    print("""Available commands:
  find [type=...] [interactive=True] [accessible=True] [depth=N]
  get <item> [props=text,enabled]
  click <item> [button=right] [double=True]
  drag <from> <to>
  swipe <item> [direction=up] [distance=200]
  keys [<item>] text=... | key=enter | shortcut=Ctrl+S
  screenshot [<item>] [save_to=path.png]
  tree [root=...] [depth=N] [props=custom]
  wait <item> [property=visible] [value=True] [timeout=5000]
  raw <method> [key=value ...]
  quit / exit / q

Items can be objectNames (plain strings) or JSON property queries:
  click saveBtn
  click {"type":"Button","text":"Save"}
""")
