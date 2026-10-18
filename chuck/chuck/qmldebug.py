"""Attach Qt's QML tooling to a tester-enabled app: profiler, debugger, live preview.

All three use the QML debug connector that Qt opens for the ``-qmljsdebugger``
argument. The app enables it by calling
``tester::enableQmlDebuggingFromArguments(argc, argv)`` first in ``main()``
(see ``tester/include/tester/QmlDebug.h``), so no ``QT_QML_DEBUG`` build is needed.

A connector serves one client at a time: profile, debug or preview, not two at once.

Usage::

    port = free_port()
    app = subprocess.Popen([exe, qmljsdebugger_arg(port, services=PROFILER_SERVICES)])
    with QmlProfiler(port) as profiler:
        profiler.start()
        ...  # drive the scenario with ChuckClient
        profiler.save("scenario.qtd")
"""

from __future__ import annotations

import os
import queue
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import xml.etree.ElementTree as ET
from collections.abc import Iterable, Sequence
from contextlib import closing
from pathlib import Path

# Plugin keys from the qmltooling plugins (the "Keys" in their metadata).
PROFILER_SERVICES = ("CanvasFrameRate", "EngineControl", "DebugMessages")
DEBUGGER_SERVICES = ("V8Debugger", "QmlDebugger", "DebugMessages")
INSPECTOR_SERVICES = ("QmlInspector",)

# What qmlprofiler records unless told otherwise. Scene graph, memory and
# animation events make a trace many times larger and rarely explain a slow page.
DEFAULT_PROFILE_FEATURES = ("javascript", "compiling", "creating", "binding", "handlingsignal", "pixmapcache")


class QmlToolError(RuntimeError):
    """A Qt QML tool could not be found, did not connect or reported a failure."""


def qmljsdebugger_arg(
    port: int | None = None,
    *,
    file: str | Path | None = None,
    block: bool = False,
    services: Iterable[str] | None = None,
    host: str = "127.0.0.1",
) -> str:
    """Build the ``-qmljsdebugger=...`` argument for the app's command line.

    Pass ``port`` for a TCP connector (always bound to ``host``, loopback by
    default, which tester insists on) or ``file`` for a local socket.
    ``block`` holds the QML engine until a client connects, which is how you
    profile or debug startup. ``services`` limits the connector to those
    plugins; use ``PROFILER_SERVICES`` when profiling, since the debugger
    service switches the JavaScript engine to the interpreter and skews timings.
    """
    if (port is None) == (file is None):
        raise ValueError("pass exactly one of port or file")
    parts = [f"port:{port}", f"host:{host}"] if port is not None else [f"file:{Path(file).as_posix()}"]
    if block:
        parts.append("block")
    if services:
        # Qt reads everything after "services:" as the list, so it goes last.
        parts.append("services:" + ",".join(services))
    return "-qmljsdebugger=" + ",".join(parts)


def free_port() -> int:
    """Pick a free TCP port on the loopback interface."""
    with closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def qt_bin_from_build(build_dir: str | Path) -> Path | None:
    """Find Qt's bin directory from the CMakeCache.txt in or above ``build_dir``."""
    for directory in [Path(build_dir).resolve(), *Path(build_dir).resolve().parents]:
        cache = directory / "CMakeCache.txt"
        if not cache.is_file():
            continue
        match = re.search(r"^Qt6Core_DIR:PATH=(.+)$", cache.read_text(encoding="utf-8", errors="replace"), re.MULTILINE)
        if not match:
            return None
        qt_bin = Path(match.group(1)).resolve().parents[2] / "bin"
        return qt_bin if qt_bin.is_dir() else None
    return None


def find_qt_tool(name: str, qt_bin: str | Path | None = None) -> Path:
    """Locate a Qt tool such as ``qmlprofiler`` or ``qmlpreview``.

    Looks in ``qt_bin``, then ``$CHUCK_QT_BIN``, then ``PATH``.
    """
    exe = name + (".exe" if sys.platform == "win32" else "")
    for directory in (qt_bin, os.environ.get("CHUCK_QT_BIN")):
        if directory and (Path(directory) / exe).is_file():
            return Path(directory) / exe
    found = shutil.which(name)
    if found:
        return Path(found)
    raise QmlToolError(f"{name} not found: pass qt_bin, set CHUCK_QT_BIN to Qt's bin directory, or put it on PATH")


class _InteractiveTool:
    """A Qt tool run with --interactive, driven through its stdin prompt."""

    def __init__(self, name: str) -> None:
        self._name = name
        self._proc: subprocess.Popen[str] | None = None
        self._lines: queue.Queue[str | None] = queue.Queue()
        self.log: list[str] = []

    def _spawn(self, cmd: Sequence[str], **popen: object) -> None:
        self._proc = subprocess.Popen(
            [str(c) for c in cmd],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            **popen,
        )
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self) -> None:
        assert self._proc and self._proc.stdout
        for line in self._proc.stdout:
            # The "> " prompt has no newline, so it prefixes the next reply.
            line = line.rstrip().removeprefix("> ")
            self.log.append(line)
            self._lines.put(line)
        self._lines.put(None)

    def _expect(self, patterns: Sequence[str], timeout: float, failures: Sequence[str] = ()) -> str:
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise QmlToolError(f"{self._name}: timed out waiting for {patterns!r}; output: {self.log[-10:]}")
            try:
                line = self._lines.get(timeout=remaining)
            except queue.Empty:
                continue
            if line is None:
                raise QmlToolError(f"{self._name} exited; output: {self.log[-10:]}")
            if any(p in line for p in patterns):
                return line
            if any(f in line for f in failures):
                raise QmlToolError(f"{self._name}: {line}")

    def _send(self, command: str) -> None:
        if not self._proc or self._proc.poll() is not None:
            raise QmlToolError(f"{self._name} is not running")
        assert self._proc.stdin
        self._proc.stdin.write(command + "\n")
        self._proc.stdin.flush()

    def close(self, timeout: float = 10.0) -> None:
        """Quit the tool, killing it if it doesn't exit within ``timeout``."""
        if not self._proc:
            return
        if self._proc.poll() is None:
            try:
                self._send("quit")
                self._proc.wait(timeout=timeout)
            except (QmlToolError, OSError, subprocess.TimeoutExpired):
                self._proc.kill()
                self._proc.wait(timeout=5)
        self._proc = None


class QmlProfiler(_InteractiveTool):
    """Attach ``qmlprofiler`` to an app's debug port and record on demand.

    Recording is off until ``start()``, so a trace covers just the scenario,
    not the app's startup. ``save()`` writes what was recorded and clears it,
    and the app keeps running. To profile startup instead, launch the app with
    ``qmljsdebugger_arg(..., block=True)`` and pass ``record=True``.

    The calls block until qmlprofiler confirms; in an async test that briefly
    stalls the event loop, which does no harm since the app is another process.
    """

    def __init__(
        self,
        port: int,
        *,
        host: str = "127.0.0.1",
        features: Iterable[str] | None = DEFAULT_PROFILE_FEATURES,
        record: bool = False,
        qt_bin: str | Path | None = None,
    ) -> None:
        super().__init__("qmlprofiler")
        self._port = port
        self._host = host
        self._features = tuple(features) if features else ()
        self._recording = record
        self._exe = find_qt_tool("qmlprofiler", qt_bin)

    def __enter__(self) -> QmlProfiler:
        self.attach()
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    @property
    def recording(self) -> bool:
        return self._recording

    def attach(self, timeout: float = 20.0) -> None:
        """Connect to the app. It must already listen on the port, or be about to."""
        cmd = [self._exe, "--attach", self._host, "--port", str(self._port), "--interactive",
               "--record", "on" if self._recording else "off"]
        if self._features:
            cmd += ["--include", ",".join(self._features)]
        self._spawn(cmd)
        self._expect(["Connected to"], timeout)

    def start(self, timeout: float = 10.0) -> None:
        """Start recording."""
        if not self._recording:
            self._send("record")
            self._expect(["Recording started"], timeout)
            self._recording = True

    def stop(self, timeout: float = 10.0) -> None:
        """Stop recording and keep the data for ``save()``."""
        if self._recording:
            self._send("record")
            self._expect(["Recording stopped"], timeout)
            self._recording = False

    def save(self, path: str | Path, timeout: float = 120.0) -> Path:
        """Stop recording, write the trace to ``path`` (.qtd) and clear it from memory.

        Large traces take a while to arrive from the app, hence the long timeout.
        """
        path = Path(path).resolve()
        path.parent.mkdir(parents=True, exist_ok=True)
        self._send(f"flush {path.as_posix()}")
        self._recording = False
        self._expect(["Data written to"], timeout, failures=["No data", "Could not", "Failed", "Cannot"])
        return path


def module_resource_map(build_dir: str | Path, output: str | Path) -> Path:
    """Write one .qrc that maps every QML module's ``qrc:`` paths back to its sources.

    ``qmlpreview`` needs that map to know which source file a ``qrc:`` URL came
    from. The .qrc files that ``qt_add_qml_module()`` generates under
    ``.qt/rcc/`` already list them, but end their prefix in a slash, which
    ``qmlpreview`` (Qt 6.12) fails to resolve. This merges them with the
    prefixes normalised.
    """
    root = ET.Element("RCC")
    seen: set[tuple[str, str]] = set()
    for qrc in sorted(Path(build_dir).glob("**/.qt/rcc/*.qrc")):
        try:
            tree = ET.parse(qrc)
        except ET.ParseError:
            continue
        for resource in tree.getroot().iter("qresource"):
            prefix = "/" + resource.get("prefix", "/").strip("/")
            out = ET.SubElement(root, "qresource", prefix=prefix)
            for entry in resource.iter("file"):
                source = Path(entry.text or "")
                if not source.is_absolute():
                    source = (qrc.parent / source).resolve()
                alias = entry.get("alias") or (entry.text or "")
                if (prefix, alias) in seen or not source.is_file():
                    continue
                seen.add((prefix, alias))
                ET.SubElement(out, "file", alias=alias).text = source.as_posix()
    output = Path(output)
    ET.ElementTree(root).write(output, encoding="utf-8", xml_declaration=False)
    return output


class QmlPreview(_InteractiveTool):
    """Run the app under ``qmlpreview``: saved QML edits show up without a rebuild.

    ``qmlpreview`` starts the app itself and adds the ``-qmljsdebugger``
    argument, so pass the app's own arguments (``--tester-port`` and the like)
    in ``args``. Give ``build_dir`` so ``qrc:`` URLs map back to the source
    files (see ``module_resource_map``), or ``resources`` for your own .qrc.

    C++ changes, ``qmldir``/module changes, CMake changes and new files still
    need a rebuild. If edits stop showing up, ``restart()``: it relaunches the
    app and replays the recorded input to get back to the same screen.
    """

    def __init__(
        self,
        program: str | Path,
        args: Sequence[str] = (),
        *,
        build_dir: str | Path | None = None,
        resources: Sequence[str | Path] = (),
        cwd: str | Path | None = None,
        env: dict[str, str] | None = None,
        qt_bin: str | Path | None = None,
    ) -> None:
        super().__init__("qmlpreview")
        self._program = Path(program)
        self._args = list(args)
        self._resources = [Path(r) for r in resources]
        self._cwd = cwd
        self._env = env
        if build_dir is not None:
            qt_bin = qt_bin or qt_bin_from_build(build_dir)
            self._tmp = tempfile.TemporaryDirectory(prefix="chuck-preview-")
            self._resources.append(module_resource_map(build_dir, Path(self._tmp.name) / "modules.qrc"))
        self._exe = find_qt_tool("qmlpreview", qt_bin)

    def __enter__(self) -> QmlPreview:
        self.start()
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    def start(self, timeout: float = 30.0) -> None:
        """Launch the app under the preview and wait until it is connected."""
        cmd: list[str | Path] = [self._exe, "--interactive"]
        for resource in self._resources:
            cmd += ["--resource", resource]
        cmd += [self._program, *self._args]
        self._spawn(cmd, cwd=self._cwd or self._program.parent, env=self._env)
        self._expect(["Connected."], timeout)

    def restart(self, timeout: float = 10.0) -> None:
        """Relaunch the app and replay the recorded input events.

        Returns once the relaunch has begun; qmlpreview reports nothing when
        the new process is up, so wait for the app (e.g. its tester port).
        """
        self._send("restart")
        self._expect(["restarting"], timeout)

    def close(self, timeout: float = 10.0) -> None:
        super().close(timeout)
        if tmp := getattr(self, "_tmp", None):
            tmp.cleanup()
