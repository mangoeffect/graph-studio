# -*- coding: utf-8 -*-
"""scenarios.py — Android 端 graph E2E 场景（adb 冷启动 + [gs] 日志断言）。

对齐 macOS E2E 的 files/run 场景与 WASM 的模块策略（scripts/e2e_macos/
scenarios.py、scripts/e2e_wasm/scenarios.py），断言通道三端同源：

| 机制 | macOS | WASM | Android |
|---|---|---|---|
| 图输入 | CLI --open/--run | URL ?open=&run=1 | runner argv（图路径） |
| 断言 | stderr 镜像文件的 [gs] 行 | 浏览器 console 的 [gs] 行 | runner stdout 的 [gs] 行 |
| 冷启动 | 每图一个进程 | 每图一个新 tab | 每图一次 adb shell |
| 计数锚点 | AX 的 "Nodes: N\\|Edges: M" | __gsTest.taskCount() | 日志行 "Graph loaded: N nodes, M edges" |
| 报告 | e2e_report → dist/e2e_macos | 同 → dist/e2e_wasm | 同 → dist/e2e_android |
| 落位 | gc.stage_copy → run_dir | 同 → serve/ | 同 → run_dir，再整树 adb push |

场景：
  boot   设备就绪 + runner ABI 与设备一致 + whole-archive 注册断言（--selfcheck）
  run    单图冒烟（生成的 tiny png 图 → 恰好 1 ok / 0 failed）
  files  逐张冷启动执行可跑的子模块单测图（每图一个用例）
  crash  --test-crash 预期 SIGSEGV

差异（Android 侧没有 GraphStudio app 壳，见 dev-docs/e2e-android.md）：GUI
编辑场景（macOS 的 core/lifecycle）不对齐——没有 app 形态可驱动，等价覆盖面
由 boot（注册/加载）+ files（全量子模块图执行）承担。
"""

from __future__ import annotations

import json
import re
from pathlib import Path

import e2e_graph_cases as gc
from gs import repo_root

from .adb import AdbDevice, AdbError

# 与 macOS/WASM 同源的完成行契约（MainWindow::onLogMessage 的 [gs] 镜像 →
# GraphViewModel::onRunSummary 的 "Run <i> finished: N ok, M failed (<ms> ms)"，
# ff18697 运行模型扩展后的形态；tg_e2e_runner 同步打同形行）。
FINISHED_RE = re.compile(r"\[gs\] Run \d+ finished:\s*(\d+)\s*ok,\s*(\d+)\s*failed")
LOADED_RE = re.compile(r"\[gs\] Graph loaded:\s*(\d+)\s*nodes,\s*(\d+)\s*edges")
SELFCHECK_RE = re.compile(r"\[gs\] selfcheck:\s*abi=(\S+)\s+tasks=(\d+)")
TASK_RE = re.compile(r"\[gs\] task:\s+(\S+)")

# subnode.json 里参与 Android e2e 的模块（= tests/android/CMakeLists.txt 的
# TG_E2E_SUBMODULES；build_android.py 的 SUBMODULE_TARGETS 是同一集合）。
# （js_script 任务层已移除；其图夹具原迁至主仓库 tests/graphs/js，现已随
# 任务一并删除。）
ANDROID_MODULES = [
    "image_reader", "image_filtering", "image_writer",
    "image_geometry", "image_color", "image_color_grading",
    # blend：CPU 路径在 Android 可用（GPU 走核心运行期后端，Android 无则回落）
    "blend",
]

# Android 侧不可执行的子模块（按 gc.discover_graphs 的 module 叶子名记 skip；
# 对齐 WASM 的 WASM_UNSUPPORTED_MODULES 机制）：
#   image_processing = gpu 子模块（无 Android GPU 后端：wgpu 无 android 产物、
#   Vulkan 走桌面 SDK 的 find_package）；render_task 的 Android 构建门未开；
#   video_io 依赖 opencv_videoio（build_opencv_android.py 的 BUILD_LIST 未含）。
#   （mp_*/js_script/mnn_* 任务层已移除，不再占用发现树条目。）
ANDROID_UNSUPPORTED_MODULES = {
    "image_processing": "gpu 子模块无 Android GPU 后端（wgpu 无 android 产物）",
    "render_task": "render 子模块 Android 构建门未开（任务类型未注册）",
    "video_io": "OpenCV Android BUILD_LIST 无 videoio 模块（video_io 未编入）",
}


class ScenarioError(RuntimeError):
    pass


class GraphCaseError(ScenarioError):
    """单图冷启动用例失败，携带现场快照（日志尾部/退出码）与关联工件。"""

    def __init__(self, msg: str, snapshot: dict | None = None,
                 artifacts: list | None = None):
        super().__init__(msg)
        self.snapshot = snapshot or {}
        self.artifacts = artifacts or []


def _tail(text: str, lines: int = 30) -> str:
    return "\n".join(text.strip().splitlines()[-lines:])


def expected_android_tasks() -> set[str]:
    """Android 构建应注册的任务集合（subnode.json ∩ ANDROID_MODULES）。

    只作为**子集断言**（whole-archive 注册保活）：subnode.json 对个别模块
    （mediapipe）是不完备清单，但"这些任务必须在"永远成立。
    """
    cfg = json.loads((repo_root() / "subnode.json").read_text(encoding="utf-8"))
    out: set[str] = set()
    for sub in cfg.get("submodules", []):
        if sub.get("name") in ANDROID_MODULES:
            out.update(sub.get("tasks", []))
    return out


class DeviceRunner:
    """Android 侧的"进程管理 + 图执行"角色（对应 macOS 的 AppRunner）。

    设备侧落位树（与主机 run_dir 结构对齐，图与资产同级的相对布局原样保留）：

      <remote_root>/tg_e2e_runner                     runner 可执行
      <remote_root>/submodule_graphs/<module>/<图>     gc.stage_copy 的副本
      <remote_root>/smoke/e2e_graph.json               run 场景的冒烟图
    """

    def __init__(self, device: AdbDevice, runner_local: Path, run_dir: Path,
                 remote_root: str):
        self.device = device
        self.runner_local = Path(runner_local)
        self.run_dir = Path(run_dir)
        self.remote_root = remote_root.rstrip("/")
        self.remote_runner = f"{self.remote_root}/tg_e2e_runner"

    # ---------- 落位 ----------

    def prepare(self) -> None:
        """推 runner + 补执行位（adb push 的权限保留因版本而异）。"""
        self.device.mkdir(self.remote_root)
        self.device.push(self.runner_local, self.remote_runner)
        self.device.chmod_exec(self.remote_runner)

    def push_run_dir(self) -> None:
        """把 run_dir 下的落位树整棵推上设备（一次推送，避免逐图 adb push 成瓶颈）。

        `adb push <local_dir> <remote_root>` 在 remote_root 已存在时落到
        `<remote_root>/<local_dir 名>`——即 submodule_graphs/ 与 smoke/ 保持同名。
        """
        for name in ("submodule_graphs", "smoke"):
            local = self.run_dir / name
            if local.is_dir():
                self.device.push(local, self.remote_root)

    # ---------- 执行 ----------

    def run_runner(self, argv: list[str], *, timeout: float = 120.0) -> tuple[int, str]:
        """在设备上跑 runner（argv 追加在 runner 路径之后），返回 (退出码, 输出)。"""
        return self.device.exec_remote([self.remote_runner, *argv], timeout=timeout)

    def graph_case(self, graph_rel: str, *, nodes=None, edges=None,
                   timeout: float = 120.0, log_name: str = "") -> tuple[int, int]:
        """冷启动执行一张图，返回 (ok, failed)。

        `graph_rel` 是相对 remote_root 的路径（如
        `submodule_graphs/image_filtering/single_filter.json`）；runner 以图的
        绝对路径为 argv，图内相对资产由框架按图目录解析（设备上布局与主机一致）。
        runner 的 stdout+stderr 落到 `run_dir/app_logs/<module>_<图名>.log`
        ——与 macOS 的 stderr 镜像日志同命名、同位置。

        失败（无完成行 / 计数不符）在退出前采集日志尾部，包进 GraphCaseError。
        """
        graph_path = Path(graph_rel)
        remote_graph = f"{self.remote_root}/{graph_rel}"
        log_file = self.run_dir / "app_logs" / (
            log_name or f"{graph_path.parent.name}_{graph_path.name}.log")
        log_file.parent.mkdir(parents=True, exist_ok=True)

        try:
            code, text = self.run_runner([remote_graph], timeout=timeout)
        except AdbError as ex:
            raise GraphCaseError(f"adb 执行失败: {ex}") from ex
        log_file.write_text(text, encoding="utf-8")

        snapshot = {"runner_exit": code, "graph": graph_rel, "app_log": _tail(text)}

        loaded = LOADED_RE.search(text)
        if nodes is not None or edges is not None:
            if not loaded:
                raise GraphCaseError("缺少 'Graph loaded: N nodes, M edges' 行"
                                     f"（runner 退出码 {code}）", snapshot)
            ln, le = int(loaded.group(1)), int(loaded.group(2))
            if (nodes is not None and ln != nodes) or (edges is not None and le != edges):
                raise GraphCaseError(f"图加载计数不符: 期望 ({nodes}, {edges}) "
                                     f"实得 ({ln}, {le})", snapshot)

        m = FINISHED_RE.search(text)
        if not m:
            raise GraphCaseError(f"未等到完成行（runner 退出码 {code}）", snapshot)
        return int(m.group(1)), int(m.group(2))


# ---------- boot：设备 + runner + 注册保活 ----------

def boot(device: AdbDevice, runner: DeviceRunner, ctx: dict) -> str:
    """设备就绪 -> runner ABI 与设备一致 -> whole-archive 注册断言。

    注册断言是整个方案最关键的护栏：Android 是静态链接，子模块的
    __attribute__((constructor)) 注册 TU 若无 --whole-archive 会被静默裁掉
    ——那时所有图都会以"未注册任务类型"失败，而不是报链接错误。
    """
    device.wait_device(timeout=float(ctx.get("boot_timeout", 180)))

    code, text = runner.run_runner(["--selfcheck"], timeout=60)
    m = SELFCHECK_RE.search(text)
    if not m:
        raise ScenarioError(f"--selfcheck 无输出（runner 退出码 {code}）: {_tail(text, 10)}")
    abi, count = m.group(1), int(m.group(2))

    dev_abi = device.abi()
    if abi != dev_abi:
        raise ScenarioError(
            f"runner ABI ({abi}) 与设备 CPU ({dev_abi}) 不匹配："
            f"设备是 {dev_abi} 时用 build_android.py --abi {dev_abi} --e2e"
            f"（x86_64 模拟器需 --also-x86-64）")

    registered = set(TASK_RE.findall(text))
    expected = expected_android_tasks()
    missing = sorted(expected - registered)
    if missing:
        raise ScenarioError(
            f"只有 {len(registered)} 个任务注册，缺少 {len(missing)} 个预期任务"
            f"（--whole-archive 注册保活失效？）: {', '.join(missing[:8])}")

    return (f"boot OK (serial={device.serial}, abi={abi}, 注册任务 {count} 个，"
            f"预期 {len(expected)} 个全部在位)")


# ---------- run：单图冒烟 ----------

def run_graph(runner: DeviceRunner, ctx: dict, graph_rel: str = "smoke/e2e_graph.json") -> str:
    """单图冒烟：冷启动 -> 日志 'Run 0 finished: 1 ok, 0 failed'。"""
    ok, failed = runner.graph_case(graph_rel, nodes=1, edges=0, timeout=90)
    if ok != 1 or failed != 0:
        raise ScenarioError(f"期望 1 ok / 0 failed，实得 {ok} ok / {failed} failed")
    return f"run OK（runner: Run 0 finished: {ok} ok, {failed} failed）"


# ---------- files：全量子模块图逐张冷启动 ----------

def files_run(runner: DeviceRunner, report, ctx: dict) -> int:
    """逐张冷启动执行可跑的子模块单测图，每张一个用例
    files/graph:<module>/<name>（发现/落位见 e2e_graph_cases.py，三端共用）。

    skip 规则：Android 未构建的模块（ANDROID_UNSUPPORTED_MODULES）+ 夹具资产
    缺失。返回实际执行的图数量。
    """
    graphs = gc.discover_graphs()
    max_graphs = int(ctx.get("max_graphs") or 0)     # 0 = 全部
    graphs_filter = (ctx.get("graphs_filter") or "").strip().lower()

    runnable = []
    for e in graphs:
        full = f"files/graph:{e['module']}/{e['name']}"
        reason = ANDROID_UNSUPPORTED_MODULES.get(e["module"])
        if reason:
            report.record(full, "skip", reason)
            continue
        if e["missing"]:
            report.record(full, "skip",
                          f"夹具资产缺失: {', '.join(e['missing'][:3])}")
            continue
        runnable.append(e)

    # 裁剪：--graphs 子串过滤优先，否则 --max-graphs 按 PRIORITY + 跨模块轮转选取
    runnable = gc.select_graphs(runnable, max_graphs, graphs_filter)

    # 落位：先全部 stage_copy，再整树一次 push（对齐 macOS 的 stage_copy，但
    # Android 可以只推一次——设备侧不共享进程状态，推送时机与执行无关）
    copies: list[tuple[dict, str]] = []
    for e in runnable:
        graph_copy = gc.stage_copy(e, report.run_dir)
        rel = graph_copy.relative_to(report.run_dir).as_posix()
        copies.append((e, rel))
    runner.push_run_dir()

    for e, rel in copies:
        full = f"files/graph:{e['module']}/{e['name']}"
        report.begin(full)
        try:
            ok, failed = runner.graph_case(
                rel, nodes=len(e["tasks"]), edges=len(e["edges"]), timeout=120)
            if failed != 0:
                raise ScenarioError(f"执行有 {failed} 个任务失败（ok={ok}）")
            report.record(full, "pass", f"ok={ok} failed={failed}")
        except Exception as ex:
            report.record(full, "fail", f"{type(ex).__name__}: {ex}",
                          exc=ex, app_state=getattr(ex, "snapshot", None) or {},
                          artifacts=list(getattr(ex, "artifacts", []) or []))
    return len(copies)


# ---------- crash：--test-crash 预期 SIGSEGV ----------

def crash(runner: DeviceRunner, ctx: dict) -> str:
    """--test-crash 应被信号终止（SIGSEGV）。

    设备侧无 waitpid 状态可比对，断言等价于 macOS 的 WTERMSIG∈{11,4}：adb
    会话退出码为 139（128+11，shell 归一）或 -11（waitstatus_to_exitcode 归一）。
    """
    code, text = runner.run_runner(["--test-crash"], timeout=60)
    if code in (139, -11, 128 + 11):
        return f"crash OK（预期 SIGSEGV，退出码 {code}）"
    raise ScenarioError(f"进程未按预期崩溃：退出码 {code}，输出: {_tail(text, 5)}")


SCENARIOS = ["boot", "run", "files", "crash"]
