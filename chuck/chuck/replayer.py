"""Replay a JSONL session file by sending each step to the AUT."""

from __future__ import annotations

import json
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .client import ChuckError
from .commands import ChuckClient


@dataclass
class StepResult:
    """Outcome of a single replay step."""

    step: int
    action: str
    item: str | None = None
    status: str = "ok"  # "ok" or "fail"
    error_code: int | None = None
    error_message: str | None = None


@dataclass
class ReplayReport:
    """Structured report of a replay run."""

    session: str
    total_steps: int
    passed: bool = True
    failed_step: int | None = None
    steps: list[StepResult] = field(default_factory=list)
    tree_dump: list[dict[str, Any]] | None = None
    screenshot_path: str | None = None

    def to_dict(self) -> dict[str, Any]:
        d: dict[str, Any] = {
            "session": self.session,
            "total_steps": self.total_steps,
            "passed": self.passed,
        }
        if self.failed_step is not None:
            d["failed_step"] = self.failed_step
        d["steps"] = [
            {k: v for k, v in s.__dict__.items() if v is not None}
            for s in self.steps
        ]
        if self.tree_dump is not None:
            d["tree_dump"] = self.tree_dump
        if self.screenshot_path is not None:
            d["screenshot"] = self.screenshot_path
        return d


async def replay(
    session_path: str | Path,
    *,
    host: str = "127.0.0.1",
    port: int = 1408,
    baseline_dir: str | Path | None = None,
    fail_on_diff: bool = False,
    report_path: str | Path | None = None,
) -> bool:
    """Replay a .jsonl session file. Returns True if all steps passed."""
    path = Path(session_path)
    steps = _load_steps(path)
    print(f"Replaying {len(steps)} steps from {path.name}")

    # Default baseline_dir next to the session file
    if baseline_dir is None:
        baseline_dir = path.parent / "baselines"

    report = ReplayReport(session=path.name, total_steps=len(steps))
    ok = True
    async with ChuckClient(host=host, port=port, baseline_dir=baseline_dir) as app:
        for i, step in enumerate(steps, 1):
            action = step.pop("action")
            step.pop("ts", None)  # timing is informational
            tag = step.pop("tag", None)
            verify = step.pop("verify", False)
            item = step.get("item")

            label = f"[{i}/{len(steps)}] {action}"
            if tag:
                label += f" ({tag})"

            try:
                # Pass tag/verify through for screenshot steps
                if action == "screenshot" and (tag or verify):
                    step["tag"] = tag
                    step["verify"] = verify

                result = await _dispatch(app, action, step)

                # Check verification result
                if action == "screenshot" and verify and isinstance(result, dict):
                    match = result.get("match")
                    score = result.get("score")
                    if result.get("baseline_created"):
                        print(f"  {label} ✓ baseline created")
                    elif match is True:
                        print(f"  {label} ✓ (score: {score:.4f})")
                    elif match is False:
                        msg = result.get("message", "visual mismatch")
                        print(f"  {label} ✗ {msg}", file=sys.stderr)
                        report.steps.append(StepResult(
                            step=i, action=action, item=item,
                            status="fail", error_message=msg,
                        ))
                        if fail_on_diff:
                            ok = False
                            report.failed_step = i
                            break
                    else:
                        print(f"  {label} ✓")
                else:
                    print(f"  {label} ✓")

                report.steps.append(StepResult(step=i, action=action, item=item))

            except ChuckError as e:
                print(f"  {label} ✗ {e}", file=sys.stderr)
                report.steps.append(StepResult(
                    step=i, action=action, item=item,
                    status="fail", error_code=e.code, error_message=e.message,
                ))
                report.failed_step = i
                ok = False
                # Capture context for repair
                await _capture_repair_context(app, report, path.parent)
                break
            except Exception as e:
                print(f"  {label} ✗ {type(e).__name__}: {e}", file=sys.stderr)
                report.steps.append(StepResult(
                    step=i, action=action, item=item,
                    status="fail", error_message=f"{type(e).__name__}: {e}",
                ))
                report.failed_step = i
                ok = False
                break

    report.passed = ok
    status = "PASSED" if ok else "FAILED"
    print(f"\nReplay {status}")

    # Write structured report if requested or if failed
    if report_path or not ok:
        out = Path(report_path) if report_path else path.with_suffix(".report.json")
        out.write_text(json.dumps(report.to_dict(), indent=2), encoding="utf-8")
        print(f"Report written to {out}")

    return ok


async def _capture_repair_context(
    app: ChuckClient,
    report: ReplayReport,
    output_dir: Path,
) -> None:
    """On failure, dump the QML tree and take a screenshot for diagnosis."""
    try:
        report.tree_dump = await app.tree(depth=6)
    except Exception:
        pass

    try:
        screenshot_path = output_dir / "repair_screenshot.png"
        await app.screenshot(save_to=str(screenshot_path))
        report.screenshot_path = str(screenshot_path)
    except Exception:
        pass


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


async def _dispatch(app: ChuckClient, action: str, params: dict[str, Any]) -> Any:
    match action:
        case "click":
            return await app.click(params.pop("item"), **params)
        case "drag":
            return await app.drag(params.pop("from"), params.pop("to", None), **params)
        case "swipe":
            return await app.swipe(params.pop("item"), **params)
        case "hover":
            return await app.hover(params.pop("item", None), **params)
        case "scroll":
            return await app.scroll(params.pop("item"), **params)
        case "window":
            return await app.window(**params)
        case "set":
            return await app.set(params["item"], params["property"], params.get("value"))
        case "invoke":
            return await app.invoke(params["item"], params["method"], *params.get("args", []))
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
