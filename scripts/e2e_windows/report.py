"""report.py — E2E 运行目录、结果记录与失败现场采集。

记录粒度到用例（core/<plugin case>、files/open:<graph>.json…）；
失败时保存：截图、UIA 树转储、应用状态快照、完整 traceback。
finish() 产出 summary.json（机器可读）+ summary.md（人读：总表 + 失败明细）。
"""

from __future__ import annotations

import json
import time
import traceback
from dataclasses import dataclass, field
from pathlib import Path

from gs import console


@dataclass
class ScenarioResult:
    name: str                       # 如 core/input_filter、files/open:gpu_chain.json
    status: str                     # pass / fail / skip
    detail: str = ""
    duration: float = 0.0
    traceback: str = ""             # 失败时的完整调用栈
    app_state: dict = field(default_factory=dict)   # 失败时的 UI 状态快照
    artifacts: list[str] = field(default_factory=list)


class Report:
    def __init__(self, base: Path):
        self.base = base
        self.base.mkdir(parents=True, exist_ok=True)
        stamp = time.strftime("%Y%m%d-%H%M%S")
        self.run_dir = self.base / stamp
        self.run_dir.mkdir(parents=True)
        self.results: list[ScenarioResult] = []
        self.meta: dict = {}
        self._t0: dict[str, float] = {}

    # ---- 用例计时 ----
    def begin(self, name: str):
        self._t0[name] = time.time()

    def _duration(self, name: str) -> float:
        t0 = self._t0.pop(name, None)
        return round(time.time() - t0, 2) if t0 else 0.0

    # ---- 工件路径 ----
    def path(self, name: str) -> Path:
        p = self.run_dir / name
        p.parent.mkdir(parents=True, exist_ok=True)
        return p

    def screenshot(self, img, name: str) -> str:
        p = self.path(name)
        img.save(str(p))
        return str(p)

    # ---- 结果记录 ----
    def record(self, name: str, status: str, detail: str = "",
               artifacts: list[str] | None = None,
               exc: BaseException | None = None,
               app_state: dict | None = None):
        r = ScenarioResult(name=name, status=status, detail=detail,
                           duration=self._duration(name),
                           traceback=("".join(traceback.format_exception(
                               type(exc), exc, exc.__traceback__))
                               if exc else ""),
                           app_state=app_state or {},
                           artifacts=artifacts or [])
        self.results.append(r)
        mark = {"pass": console.ok, "fail": console.fail, "skip": console.warn}[status]
        extra = f" — {detail}" if detail else ""
        timing = f"（{r.duration}s）" if r.duration else ""
        mark(f"[{name}] {status}{timing}{extra}")

    # ---- 汇总 ----
    def _counts(self) -> dict:
        return {
            "passed": sum(1 for r in self.results if r.status == "pass"),
            "failed": sum(1 for r in self.results if r.status == "fail"),
            "skipped": sum(1 for r in self.results if r.status == "skip"),
        }

    def finish(self) -> int:
        counts = self._counts()
        summary = {
            "meta": self.meta,
            "run_dir": str(self.run_dir),
            **counts,
            "results": [{
                "name": r.name, "status": r.status, "detail": r.detail,
                "duration": r.duration, "app_state": r.app_state,
                "artifacts": r.artifacts, "traceback": r.traceback,
            } for r in self.results],
        }
        self.path("summary.json").write_text(
            json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
        self.path("summary.md").write_text(self._markdown(summary), encoding="utf-8")
        console.step(f"E2E 报告目录: {self.run_dir}")
        console.step(f"汇总: {counts['passed']} passed / {counts['failed']} failed"
                     f" / {counts['skipped']} skipped"
                     f"（详见 summary.json / summary.md）")
        return 1 if counts["failed"] else 0

    def _markdown(self, summary: dict) -> str:
        lines = ["# GraphStudio E2E 报告", ""]
        meta = summary.get("meta", {})
        if meta:
            lines += ["| 项 | 值 |", "|---|---|"]
            for k, v in meta.items():
                lines.append(f"| {k} | {v} |")
            lines.append("")
        lines.append(f"结果：**{summary['passed']} passed / "
                     f"{summary['failed']} failed / {summary['skipped']} skipped**")
        lines += ["", "## 用例总表", "",
                  "| 用例 | 状态 | 耗时(s) | 说明 |", "|---|---|---|---|"]
        for r in self.results:
            detail = (r.detail or "").replace("|", "\\|").replace("\n", " ")
            if len(detail) > 120:
                detail = detail[:117] + "..."
            lines.append(f"| {r.name} | {r.status} | {r.duration} | {detail} |")

        failures = [r for r in self.results if r.status == "fail"]
        if failures:
            lines += ["", "## 失败明细", ""]
            for i, r in enumerate(failures, 1):
                lines += [f"### {i}. {r.name}", ""]
                if r.detail:
                    lines += [f"**失败原因**: {r.detail}", ""]
                if r.traceback:
                    lines += ["**调用栈**:", "", "```python",
                              r.traceback.rstrip(), "```", ""]
                if r.app_state:
                    lines += ["**失败时应用状态**:", ""]
                    state = dict(r.app_state)
                    if "log_tail" in state:
                        lines += ["- 日志尾部:", "", "```text",
                                  str(state.pop("log_tail")).rstrip(), "```"]
                    for k, v in state.items():
                        lines.append(f"- {k}: {v}")
                    lines.append("")
                if r.artifacts:
                    lines += ["**关联工件**:"]
                    lines += [f"- {a}" for a in r.artifacts]
                    lines.append("")
        skips = [r for r in self.results if r.status == "skip"]
        if skips:
            lines += ["", "## 跳过项", ""]
            lines += [f"- `{r.name}`: {r.detail or '未注明原因'}" for r in skips]
        return "\n".join(lines) + "\n"
