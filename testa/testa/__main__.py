"""CLI entry point for testa."""

from __future__ import annotations

import argparse
import asyncio
import sys


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        prog="testa",
        description="Python controller for chuck — AI-driven Qt/QML testing",
    )
    parser.add_argument("--version", action="version", version="%(prog)s 0.1.0")

    sub = parser.add_subparsers(dest="command")

    # --- replay ---
    p_replay = sub.add_parser("replay", help="Replay a recorded JSONL session")
    p_replay.add_argument("session", help="Path to .jsonl session file")
    p_replay.add_argument("--host", default="127.0.0.1")
    p_replay.add_argument("--port", type=int, default=9223)

    # --- record ---
    p_record = sub.add_parser("record", help="Record an interactive session to JSONL")
    p_record.add_argument("--output", "-o", required=True, help="Output .jsonl file")
    p_record.add_argument("--host", default="127.0.0.1")
    p_record.add_argument("--port", type=int, default=9223)

    # --- export ---
    p_export = sub.add_parser("export", help="Convert JSONL to a Python test script")
    p_export.add_argument("session", help="Path to .jsonl session file")
    p_export.add_argument("--to-python", required=True, dest="output", help="Output .py file")

    # --- repl ---
    p_repl = sub.add_parser("repl", help="Interactive REPL for manual testing")
    p_repl.add_argument("--host", default="127.0.0.1")
    p_repl.add_argument("--port", type=int, default=9223)

    args = parser.parse_args(argv)

    if args.command is None:
        parser.print_help()
        sys.exit(0)

    if args.command == "replay":
        from .replayer import replay

        asyncio.run(replay(args.session, host=args.host, port=args.port))
    elif args.command == "record":
        print(f"Recording to {args.output} ... (not yet implemented)")
    elif args.command == "export":
        from .exporter import export_to_python

        export_to_python(args.session, args.output)
    elif args.command == "repl":
        from .repl import run_repl

        asyncio.run(run_repl(host=args.host, port=args.port))


if __name__ == "__main__":
    main()
