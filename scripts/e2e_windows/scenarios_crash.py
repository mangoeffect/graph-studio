"""scenarios_crash.py — 安装态崩溃上报（--test-crash + 不可达 dummy DSN）。

复用 verify_crash_reporting.py 的手法，但跑的是安装目录里的 exe（直启，
full-trust 打包应用可直接运行，包身份随进程令牌生效）。dump 落点需轮询两处：
真实 %LOCALAPPDATA%\\GraphStudio\\sentry_db，以及 MSIX 虚拟化重定向的
%LOCALAPPDATA%\\Packages\\<PFN>\\LocalCache\\Local\\GraphStudio\\sentry_db。
"""

from __future__ import annotations

import os
import subprocess
import time
from pathlib import Path

from gs import console

DUMMY_DSN = "http://00000000000000000000000000000000@127.0.0.1:9/1"
DUMP_WAIT_SECONDS = 25
RUN_TIMEOUT = 120


def sentry_db_candidates(pkg) -> list[Path]:
    la = Path(os.environ["LOCALAPPDATA"])
    return [
        la / "GraphStudio" / "sentry_db",
        la / "Packages" / pkg.pfn / "LocalCache" / "Local" / "GraphStudio" / "sentry_db",
    ]


def find_dumps(roots: list[Path]) -> list[Path]:
    out = []
    for r in roots:
        if r.is_dir():
            out.extend(r.rglob("*.dmp"))
    return sorted(out)


def trigger_crash(pkg, report) -> subprocess.CompletedProcess:
    env = os.environ.copy()
    env["SENTRY_DSN"] = DUMMY_DSN
    env["SENTRY_DEBUG"] = "1"
    # 不重定向 LOCALAPPDATA（要验证真实/虚拟化两个落点）；不用 offscreen（无 UI）。
    console.step(f"直启安装态 exe --test-crash: {pkg.exe}")
    return subprocess.run([str(pkg.exe), "--test-crash"], env=env,
                          capture_output=True, text=True, timeout=RUN_TIMEOUT)


def run(pkg, report, fixtures, ctx) -> None:
    proc = trigger_crash(pkg, report)
    output = (proc.stdout or "") + (proc.stderr or "")
    report.path("crash_output.txt").write_text(output, encoding="utf-8", errors="replace")

    if "[CrashReporter] initialized" not in output:
        raise AssertionError("stderr 未出现 '[CrashReporter] initialized'（安装包未含"
                             "崩溃上报或 DSN 未生效）")
    if proc.returncode == 0:
        raise AssertionError(f"进程正常退出 (code 0)，期望崩溃退出码")
    console.ok(f"崩溃退出码 0x{proc.returncode & 0xFFFFFFFF:X}")

    roots = sentry_db_candidates(pkg)
    dumps: list[Path] = []
    deadline = time.time() + DUMP_WAIT_SECONDS
    while time.time() < deadline and not dumps:
        dumps = find_dumps(roots)
        if not dumps:
            time.sleep(1.0)
    if not dumps:
        for r in roots:
            console.warn(f"未找到 dump；候选目录存在性 {r}: {r.is_dir()}")
        raise AssertionError(f"两处 sentry_db 候选均无 *.dmp: {[str(r) for r in roots]}")

    root_hit = next(r for r in roots if r in dumps[0].parents)
    virtualized = "LocalCache" in str(dumps[0])
    for d in dumps:
        console.ok(f"minidump: {d} ({d.stat().st_size} bytes)")
    console.ok(f"落点: {'MSIX 虚拟化 LocalCache' if virtualized else '真实 AppData'}")
    # 交给 lifecycle 场景：包内工件（LocalCache）在升级后应保留、卸载后应清理
    ctx["crash_dump"] = dumps[0]
    ctx["crash_dump_package_scoped"] = virtualized
