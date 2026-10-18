"""Summarise a qmlprofiler trace (.qtd) without Qt Creator.

A .qtd is XML: ``<event>`` elements describe what ran (type, file, line,
details) and ``<range startTime duration eventIndex>`` elements are single
occurrences, in nanoseconds. Ranges nest; self time is a range's duration
minus that of its direct children.

For a full analysis against the sources, the qt-qml-profiler skill goes
further; this is the quick answer to "where did the time go".
"""

from __future__ import annotations

import collections
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path


@dataclass(frozen=True)
class QmlEvent:
    type: str
    file: str
    line: int
    details: str

    @property
    def label(self) -> str:
        where = self.file.rsplit("/", 1)[-1]
        return f"{self.type} {where}:{self.line} {self.details[:70]}".rstrip()


@dataclass
class EventStats:
    event: QmlEvent
    self_ms: float
    total_ms: float
    calls: int


@dataclass
class LongRange:
    start_s: float
    duration_ms: float
    event: QmlEvent


@dataclass
class TraceSummary:
    duration_s: float
    busy_ms: float
    self_ms_by_type: dict[str, float] = field(default_factory=dict)
    top_self: list[EventStats] = field(default_factory=list)
    long_ranges: list[LongRange] = field(default_factory=list)

    def format(self) -> str:
        lines = [f"trace {self.duration_s:.2f} s, QML busy (top level) {self.busy_ms:.0f} ms", "--- self time by type"]
        lines += [f"{ms:9.1f} ms  {kind}" for kind, ms in self.self_ms_by_type.items()]
        lines.append("--- top by self time")
        lines += [f"{s.self_ms:9.1f} ms self {s.total_ms:9.1f} total {s.calls:6}x  {s.event.label}" for s in self.top_self]
        lines.append("--- long top-level ranges")
        lines += [f"{r.start_s:8.3f}s {r.duration_ms:8.1f} ms  {r.event.label}" for r in self.long_ranges]
        return "\n".join(lines)


def summarize(
    path: str | Path,
    *,
    start_s: float = 0.0,
    end_s: float | None = None,
    top: int = 40,
    long_ms: float = 40.0,
) -> TraceSummary:
    """Summarise the ranges that start between ``start_s`` and ``end_s``.

    Times are seconds from the trace start. Read ``long_ranges`` first: one
    long top-level range is usually the culprit, and a second pass with the
    window narrowed to it shows what it spent the time on.
    """
    root = ET.parse(path).getroot()
    trace_start = int(root.get("traceStart", "0"))
    trace_end = int(root.get("traceEnd", "0"))
    events = {
        int(e.get("index", "-1")): QmlEvent(
            type=e.findtext("type", "?"),
            file=e.findtext("filename", ""),
            line=int(e.findtext("line", "0") or 0),
            details=e.findtext("details", ""),
        )
        for e in root.iter("event")
    }

    lo = trace_start + start_s * 1e9
    hi = trace_start + end_s * 1e9 if end_s is not None else float("inf")
    ranges = sorted(
        (
            (int(r.get("startTime", "0")), int(r.get("duration", "0")), int(r.get("eventIndex", "-1")))
            for r in root.iter("range")
            if r.get("duration") and int(r.get("eventIndex", "-1")) in events
        ),
        key=lambda r: (r[0], -r[1]),
    )
    window = [r for r in ranges if lo <= r[0] < hi]

    self_ns: collections.Counter[int] = collections.Counter()
    total_ns: collections.Counter[int] = collections.Counter()
    calls: collections.Counter[int] = collections.Counter()
    by_type: collections.Counter[str] = collections.Counter()
    stack: list[list[int]] = []  # [end, eventIndex, childTime, duration]
    top_level: list[tuple[int, int, int]] = []

    def pop() -> None:
        _, index, child, duration = stack.pop()
        self_ns[index] += duration - child
        by_type[events[index].type] += duration - child

    for begin, duration, index in window:
        while stack and stack[-1][0] <= begin:
            pop()
        if stack:
            stack[-1][2] += duration
        else:
            top_level.append((begin, duration, index))
        stack.append([begin + duration, index, 0, duration])
        total_ns[index] += duration
        calls[index] += 1
    while stack:
        pop()

    return TraceSummary(
        duration_s=max(0, trace_end - trace_start) / 1e9,
        busy_ms=sum(d for _, d, _ in top_level) / 1e6,
        self_ms_by_type={kind: ns / 1e6 for kind, ns in by_type.most_common()},
        top_self=[
            EventStats(events[i], ns / 1e6, total_ns[i] / 1e6, calls[i]) for i, ns in self_ns.most_common(top)
        ],
        long_ranges=[
            LongRange((b - trace_start) / 1e9, d / 1e6, events[i]) for b, d, i in top_level if d > long_ms * 1e6
        ],
    )
