#!/usr/bin/env python3
"""run_e2e_macos.py — GraphStudio macOS 安装态 E2E（AXAPI + CGEvent）。

镜像 scripts/e2e_windows/ 的定位：对**真实 .app**（dev 构建或 dmg 安装产物）
做端到端黑盒——原生菜单、真实鼠标键盘、AX 控件树断言。
驱动实现见 scripts/e2e_macos/ax_driver.py（ctypes 直调 AXAPI/CGEvent，零依赖）。

图输入走 CLI 启动参数（app 侧 --open <graph.json> [--run]）：不做 NSOpenPanel
自动化（前往 sheet/懒加载行/Open 按钮全是时序竞态，教训见 dev-docs）。

场景:
  core      画布手势（右键建点 -> 端口拖线 -> undo/redo）
  files     全部子模块单测图逐张 --open --run 冷启动（69 张/10 模块，对齐
            e2e_windows；--max-graphs/--graphs 可裁剪）
  run       单图冒烟（生成的 tiny png 图）
  lifecycle Cmd+Q 正常退出
  crash     --test-crash 预期 SIGSEGV

结果落 report（对齐 e2e_windows/report.py）：dist/e2e_macos/<时间戳>/ 下
summary.json + summary.md（用例总表 / 失败明细含截图与日志尾部 / 跳过项），
退出码 0 = 全部通过。

权限（一次性）: 运行本脚本的终端/IDE 需要「系统设置 -> 隐私与安全性 ->
辅助功能」授权（AX 读取与 CGEvent 注入都受 TCC 管控）。未授权时脚本
会触发系统弹窗引导。

用法:
  python scripts/run_e2e_macos.py                         # 全部场景（dev .app）
  python scripts/run_e2e_macos.py --scenario core,files   # 选场景
  python scripts/run_e2e_macos.py --max-graphs 10         # files 裁剪到 10 张
  python scripts/run_e2e_macos.py --graphs opencv         # files 按路径子串过滤
  python scripts/run_e2e_macos.py --app /Applications/GraphStudio.app
"""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, platform, repo_root, sdk  # noqa: E402
from e2e_macos import ax_driver, scenarios as sc  # noqa: E402
from e2e_macos.ax_driver import MacDriver, ensure_accessibility  # noqa: E402
from e2e_macos.report import Report  # noqa: E402

ALL_SCENARIOS = sc.SCENARIOS + ["crash"]


class AppRunner:
    """E2E 的 app 进程管理：posix_spawn 启动、SIGTERM 收尾 + 僵尸回收。

    files/run 场景逐图冷启动（--open/--run），core/lifecycle 场景复用一个
    交互实例。批跑前必须 stop()——两个 GraphStudio 抢焦点会让菜单/键盘
    注入落到错误的实例上。
    """

    def __init__(self, binary: Path, env: dict, run_dir: Path):
        self.binary = binary
        self.env = env
        self.run_dir = run_dir   # app_logs/ 镜像日志的落盘根
        self.pid = None

    def start(self, *extra: str, log=None) -> int:
        """启动 app；log 给路径时把子进程 stdout/stderr dup2 到该文件
        （app 的 [gs] qInfo 镜像——执行完成断言的可靠通道）。"""
        self.stop()
        actions = []
        fd = None
        if log is not None:
            log.parent.mkdir(parents=True, exist_ok=True)
            fd = os.open(log, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
            actions = [(os.POSIX_SPAWN_DUP2, fd, 1),
                       (os.POSIX_SPAWN_DUP2, fd, 2)]
        try:
            self.pid = os.posix_spawn(str(self.binary),
                                      [str(self.binary), *extra], self.env,
                                      file_actions=actions)
        finally:
            if fd is not None:
                os.close(fd)
        return self.pid

    def running(self) -> bool:
        if self.pid is None:
            return False
        try:
            done, _ = os.waitpid(self.pid, os.WNOHANG)  # 顺带回收已退出子进程
        except ChildProcessError:
            self.pid = None
            return False
        if done == self.pid:
            self.pid = None
            return False
        return True

    def stop(self, timeout: float = 10.0):
        """SIGTERM 收尾（超时升格 SIGKILL）。SIGTERM 不产生 crashpad
        minidump（sentry-native 默认不挂 SIGTERM handler），批跑退出即用；
        优雅退出路径由 lifecycle 场景单独覆盖。"""
        pid, self.pid = self.pid, None
        if pid is None:
            return
        try:
            os.kill(pid, 15)
        except ProcessLookupError:
            pass
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                done, _ = os.waitpid(pid, os.WNOHANG)
            except ChildProcessError:
                return
            if done == pid:
                return
            time.sleep(0.2)
        try:
            os.kill(pid, 9)
            os.waitpid(pid, 0)
        except (ProcessLookupError, ChildProcessError):
            pass

    def ensure_attached(self, timeout: float = 45.0):
        """core/lifecycle 用：保证一个 plain 实例在跑，返回 (drv, window)。"""
        if not self.running():
            self.start()
        drv = MacDriver(self.pid)
        window = drv.window_by_title("Graph Studio", timeout=timeout)
        return drv, window


def default_app(root: Path) -> Path:
    return root / "app" / "graph_studio" / "build" / "graph_studio.app"


def launch_env(root: Path, config: str) -> dict:
    """复刻 run_graph_studio.py 的 dev 运行环境（插件/模型/动态库路径）。"""
    env = dict(os.environ)
    env.pop("QT_QPA_PLATFORM", None)  # 必须真实 GUI
    lib_build = root / "build"
    plugin_dirs = sdk.plugin_build_dirs(lib_build, config)
    if plugin_dirs:
        env["TASK_GRAPH_PLUGINS_PATH"] = os.pathsep.join(str(p) for p in plugin_dirs)
    dev_models = root / "submodules" / "mediapipe" / "mediapipe_vision" / "tests" / "models"
    if dev_models.is_dir():
        env["GRAPH_STUDIO_MODELS_DIR"] = str(dev_models)
    lib_env = platform.runtime_lib_env()
    if lib_env:
        existing = env.get(lib_env, "")
        parts = [str(lib_build / config)]
        env[lib_env] = os.pathsep.join(parts + ([existing] if existing else []))
    return env


def wait_exit(pid: int, timeout: float = 15.0) -> int:
    """等待（posix_spawn 的）子进程退出，返回退出状态。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        done, status = os.waitpid(pid, os.WNOHANG)
        if done == pid:
            return status
        time.sleep(0.3)
    raise TimeoutError("进程未在限时内退出")


def tiny_png(width: int, height: int) -> bytes:
    import struct
    import zlib
    raw = b""
    for y in range(height):
        raw += b"\x00"
        for x in range(width):
            raw += bytes(((x * 4) % 256, (y * 4) % 256, 128))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def write_graph(graph_path: Path, input_png: Path):
    graph_path.write_text(
        '{\n  "version": "2.0",\n  "tasks": [\n    { "id": "img", '
        '"type": "opencv_image_read",\n      "params": { "file_path": "'
        + str(input_png) + '" } }\n  ],\n  "edges": [],\n  "positions": '
        '{ "img": { "x": 0, "y": 0, "type": "opencv_image_read" } }\n}\n')


def screenshot(path: Path) -> str:
    """全屏截图（失败现场），返回路径供 report 关联。"""
    try:
        subprocess.run(["screencapture", "-x", str(path)], capture_output=True)
        console.step(f"现场截图: {path}")
    except OSError:
        pass
    return str(path)


def aggregate(report: Report, scenario: str):
    """场景级状态聚合自子用例（files/open:xxx…），对齐 e2e_windows 入口。"""
    inner = [r for r in report.results if r.name.startswith(scenario + "/")]
    failed = [r for r in inner if r.status == "fail"]
    passed = [r for r in inner if r.status == "pass"]
    if failed:
        report.record(scenario, "fail", f"{len(failed)}/{len(inner)} 子用例失败")
    else:
        report.record(scenario, "pass",
                      f"{len(passed)} 个子用例通过" if inner else "")


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="GraphStudio macOS 安装态 E2E")
    ap.add_argument("--app", default="", help=".app 路径（默认 dev 构建）")
    ap.add_argument("--scenario", default="all",
                    help=f"{','.join(ALL_SCENARIOS)} 组合，默认 all")
    ap.add_argument("--config", default="Release",
                    help="dev 构建配置（决定插件/库目录）")
    ap.add_argument("--artifacts", default="",
                    help="report 输出根目录（默认 <repo>/dist/e2e_macos）")
    ap.add_argument("--max-graphs", type=int, default=0,
                    help="files 场景纳入的子模块图数量上限（默认 0=全部，"
                         "read_image/unicode 优先）")
    ap.add_argument("--graphs", default="",
                    help="files 场景按路径子串过滤子模块图（如 gpu、mediapipe）")
    args = ap.parse_args()

    root = repo_root()
    app = Path(args.app) if args.app else default_app(root)
    binary = app / "Contents" / "MacOS" / "graph_studio"
    if not binary.is_file():
        console.fail(f"未找到可执行文件: {binary}（先构建或用 --app 指定）")
        return 1

    names = (ALL_SCENARIOS if args.scenario == "all"
             else [s.strip() for s in args.scenario.split(",")])
    for n in names:
        if n not in ALL_SCENARIOS:
            console.fail(f"未知场景: {n}（可选 {', '.join(ALL_SCENARIOS)}）")
            return 1

    report = Report(Path(args.artifacts) if args.artifacts
                    else root / "dist" / "e2e_macos")
    report.meta = {"app": str(app), "config": args.config,
                   "scenarios": ",".join(names),
                   "max_graphs": args.max_graphs or "all",
                   "graphs_filter": args.graphs or ""}
    console.step(f"E2E 运行目录: {report.run_dir}")

    # run 场景的冒烟图（生成的 tiny png 输入）
    graph_path = report.path("smoke/e2e_graph.json")
    input_png = report.path("smoke/e2e_input.png")
    input_png.write_bytes(tiny_png(64, 64))
    write_graph(graph_path, input_png)

    if not ensure_accessibility(prompt=True):
        console.fail("本终端没有辅助功能权限：系统设置 -> 隐私与安全性 -> "
                     "辅助功能，勾选运行本脚本的应用（终端/iTerm/VSCode…）")
        return 2

    ctx = {"max_graphs": args.max_graphs, "graphs_filter": args.graphs,
           "screenshot": lambda name: screenshot(report.path(name))}

    main_names = [n for n in names if n != "crash"]
    if main_names:
        runner = AppRunner(binary, launch_env(root, args.config), report.run_dir)
        console.step(f"启动 {binary}")
        try:
            for name in main_names:
                console.step(f"场景 {name}")
                report.begin(name)
                try:
                    if name == "core":
                        drv, window = runner.ensure_attached()
                        report.record(name, "pass", sc.core(drv, window,
                                                             report.run_dir))
                    elif name == "files":
                        runner.stop()  # 交互实例退场，批跑独占（避免抢焦点）
                        count = sc.files_run(runner, report, ctx)
                        aggregate(report, "files")
                        console.step(f"files: 执行 {count} 张图")
                    elif name == "run":
                        runner.stop()
                        report.record(name, "pass",
                                      sc.run_graph(runner, report, ctx,
                                                   graph_path))
                    elif name == "lifecycle":
                        drv, window = runner.ensure_attached()
                        report.record(name, "pass",
                                      sc.lifecycle(drv, window, report.run_dir))
                        status = wait_exit(runner.pid, timeout=15)
                        runner.pid = None
                        if not (os.WIFEXITED(status)
                                and os.WEXITSTATUS(status) == 0):
                            raise sc.ScenarioError(
                                f"进程退出状态异常: {status}")
                except Exception as e:   # 用例级兜底：意外异常也记录后继续
                    report.record(name, "fail", f"{type(e).__name__}: {e}",
                                  exc=e, app_state=getattr(e, "snapshot", None))
                    screenshot(report.path(f"{name}_fail.png"))
                    runner.stop()
        finally:
            runner.stop()

    # crash：独立进程跑 --test-crash，预期 SIGSEGV 非正常退出
    if "crash" in names:
        console.step("场景 crash（--test-crash，预期崩溃）")
        report.begin("crash")
        env = launch_env(root, args.config)
        pid = os.posix_spawn(str(binary), [str(binary), "--test-crash"], env)
        try:
            status = wait_exit(pid, timeout=30)
        except TimeoutError:
            os.kill(pid, 9)
            report.record("crash", "fail", "进程未崩溃退出（30s 仍在运行）")
        else:
            crashed = os.WIFSIGNALED(status) and \
                os.WTERMSIG(status) in (11, 4)  # SIGSEGV / SIGTRAP
            if crashed:
                report.record("crash", "pass", "预期 SIGSEGV 发生")
            else:
                report.record("crash", "fail", f"异常退出方式 status={status}")

    return report.finish()


if __name__ == "__main__":
    sys.exit(main())
