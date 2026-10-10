"""CLI entry point for chuck."""

from __future__ import annotations

import argparse
import asyncio
import sys


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        prog="chuck",
        description="Python controller for chuck_tester — AI-driven Qt/QML testing",
    )
    parser.add_argument("--version", action="version", version="%(prog)s 0.2.0")

    sub = parser.add_subparsers(dest="command")

    # --- replay ---
    p_replay = sub.add_parser("replay", help="Replay a recorded JSONL session")
    p_replay.add_argument("session", help="Path to .jsonl session file")
    p_replay.add_argument("--host", default="127.0.0.1")
    p_replay.add_argument("--port", type=int, default=1408)
    p_replay.add_argument("--baseline-dir", default=None, help="Directory for baseline screenshots")
    p_replay.add_argument("--fail-on-diff", action="store_true", help="Fail replay on visual mismatch")

    # --- record ---
    p_record = sub.add_parser("record", help="Record an interactive session to JSONL")
    p_record.add_argument("--output", "-o", required=True, help="Output .jsonl file")
    p_record.add_argument("--host", default="127.0.0.1")
    p_record.add_argument("--port", type=int, default=1408)

    # --- export ---
    p_export = sub.add_parser("export", help="Convert JSONL to a Python test script")
    p_export.add_argument("session", help="Path to .jsonl session file")
    p_export.add_argument("--to-python", required=True, dest="output", help="Output .py file")

    # --- repl ---
    p_repl = sub.add_parser("repl", help="Interactive REPL for manual testing")
    p_repl.add_argument("--host", default="127.0.0.1")
    p_repl.add_argument("--port", type=int, default=1408)

    # --- baseline ---
    p_baseline = sub.add_parser("baseline", help="Capture or update baseline screenshots")
    p_baseline.add_argument("session", help="Path to .jsonl session file")
    p_baseline.add_argument("--host", default="127.0.0.1")
    p_baseline.add_argument("--port", type=int, default=1408)
    p_baseline.add_argument("--baseline-dir", default=None, help="Directory for baseline screenshots")

    # --- verify ---
    p_verify = sub.add_parser("verify", help="Compare screenshots against baselines (CI mode)")
    p_verify.add_argument("session", help="Path to .jsonl session file")
    p_verify.add_argument("--host", default="127.0.0.1")
    p_verify.add_argument("--port", type=int, default=1408)
    p_verify.add_argument("--baseline-dir", default=None, help="Directory for baseline screenshots")
    p_verify.add_argument("--method", default="ssim", choices=["ssim", "phash", "exact"])
    p_verify.add_argument("--threshold", type=float, default=0.95)

    # --- mcp ---
    p_mcp = sub.add_parser("mcp", help="Start the MCP server (stdio transport) for AI agents")
    p_mcp.add_argument("--host", default="127.0.0.1", help="Chuck server host")
    p_mcp.add_argument("--port", type=int, default=1408, help="Chuck server port")
    p_mcp.add_argument("--record", default=None, metavar="FILE", help="Record all tool calls to a JSONL file")

    # --- repair ---
    p_repair = sub.add_parser("repair", help="Replay a session, capture failure context for AI repair")
    p_repair.add_argument("session", help="Path to .jsonl session file")
    p_repair.add_argument("--host", default="127.0.0.1")
    p_repair.add_argument("--port", type=int, default=1408)
    p_repair.add_argument("--report", default=None, metavar="FILE", help="Output path for the repair report JSON")

    args = parser.parse_args(argv)

    if args.command is None:
        parser.print_help()
        sys.exit(0)

    if args.command == "replay":
        from .replayer import replay

        ok = asyncio.run(replay(
            args.session,
            host=args.host,
            port=args.port,
            baseline_dir=args.baseline_dir,
            fail_on_diff=args.fail_on_diff,
        ))
        sys.exit(0 if ok else 1)
    elif args.command == "record":
        print(f"Recording to {args.output} ... (not yet implemented)")
    elif args.command == "export":
        from .exporter import export_to_python

        export_to_python(args.session, args.output)
    elif args.command == "repl":
        from .repl import run_repl

        asyncio.run(run_repl(host=args.host, port=args.port))
    elif args.command == "baseline":
        from .replayer import replay

        # Replay the session to capture baselines (verify=false by spec,
        # but tagged screenshots auto-create baselines when missing)
        asyncio.run(replay(
            args.session,
            host=args.host,
            port=args.port,
            baseline_dir=args.baseline_dir,
        ))
    elif args.command == "verify":
        from .replayer import replay

        ok = asyncio.run(replay(
            args.session,
            host=args.host,
            port=args.port,
            baseline_dir=args.baseline_dir,
            fail_on_diff=True,
        ))
        sys.exit(0 if ok else 1)
    elif args.command == "mcp":
        from .mcp_server import configure, run_server

        configure(host=args.host, port=args.port, record=args.record)
        run_server()
    elif args.command == "repair":
        from .replayer import replay

        ok = asyncio.run(replay(
            args.session,
            host=args.host,
            port=args.port,
            report_path=args.report,
            fail_on_diff=True,
        ))
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
