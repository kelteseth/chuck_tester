"""CLI entry point for chuck."""

from __future__ import annotations

import argparse
import asyncio
import sys
import time


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

    # --- profile ---
    p_profile = sub.add_parser("profile", help="Record a QML profiler trace from a running app")
    p_profile.add_argument("--qml-port", type=int, required=True, help="Port the app got in -qmljsdebugger=port:N")
    p_profile.add_argument("--qml-host", default="127.0.0.1")
    p_profile.add_argument("--output", "-o", required=True, help="Output .qtd file")
    p_profile.add_argument("--include", default=None, help="qmlprofiler features, comma separated (default: QML only)")
    p_profile.add_argument("--duration", type=float, default=None, help="Seconds to record (default: until Enter)")
    p_profile.add_argument("--qt-bin", default=None, help="Qt bin directory holding qmlprofiler")

    # --- qtd ---
    p_qtd = sub.add_parser("qtd", help="Summarise a QML profiler trace (.qtd)")
    p_qtd.add_argument("trace", help="Path to the .qtd file")
    p_qtd.add_argument("--from", dest="start", type=float, default=0.0, help="Window start, seconds from trace start")
    p_qtd.add_argument("--to", dest="end", type=float, default=None, help="Window end, seconds from trace start")
    p_qtd.add_argument("--top", type=int, default=40, help="Rows in the self-time list")
    p_qtd.add_argument("--long-ms", type=float, default=40.0, help="Report top-level ranges longer than this")

    # --- preview ---
    p_preview = sub.add_parser("preview", help="Run an app under qmlpreview so QML edits apply live")
    p_preview.add_argument("--build-dir", default=None, help="Build dir, to map qrc: URLs back to the sources")
    p_preview.add_argument("--resource", action="append", default=[], help="Extra .qrc mapping qrc: paths to files")
    p_preview.add_argument("--qt-bin", default=None, help="Qt bin directory holding qmlpreview")
    p_preview.add_argument("program", help="The app executable")
    p_preview.add_argument("args", nargs=argparse.REMAINDER, help="Arguments for the app")

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
    elif args.command == "profile":
        from .qmldebug import DEFAULT_PROFILE_FEATURES, QmlProfiler
        from .qtd import summarize

        features = args.include.split(",") if args.include else DEFAULT_PROFILE_FEATURES
        with QmlProfiler(args.qml_port, host=args.qml_host, features=features, qt_bin=args.qt_bin) as profiler:
            profiler.start()
            if args.duration is not None:
                print(f"Recording for {args.duration:g} s ...")
                time.sleep(args.duration)
            else:
                input("Recording. Press Enter to stop ... ")
            path = profiler.save(args.output)
        print(f"Wrote {path}")
        print(summarize(path).format())
    elif args.command == "qtd":
        from .qtd import summarize

        print(summarize(args.trace, start_s=args.start, end_s=args.end, top=args.top, long_ms=args.long_ms).format())
    elif args.command == "preview":
        from .qmldebug import QmlPreview

        with QmlPreview(args.program, args.args, build_dir=args.build_dir, resources=args.resource,
                        qt_bin=args.qt_bin) as preview:
            try:
                while True:
                    input("Live preview running. Enter restarts the app, Ctrl+C quits ... ")
                    preview.restart()
            except (EOFError, KeyboardInterrupt):
                pass
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
