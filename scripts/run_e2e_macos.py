#!/usr/bin/env python3
"""run_e2e_macos.py — GraphStudio macOS 安装态 E2E（AXAPI + CGEvent）。

镜像 scripts/e2e_windows/ 的定位：对**真实 .app**（dev 构建或 dmg 安装产物）
做端到端黑盒——原生菜单、NSOpenPanel、真实鼠标键盘、AX 控件树断言。
驱动实现见 scripts/e2e_macos/ax_driver.py（ctypes 直调 AXAPI/CGEvent，零依赖）。

权限（一次性）: 运行本脚本的终端/IDE 需要「系统设置 -> 隐私与安全性 ->
辅助功能」授权（AX 读取与 CGEvent 注入都受 TCC 管控）。未授权时脚本
会触发系统弹窗引导。

用法:
  python scripts/run_e2e_macos.py                         # 全部场景（dev .app）
  python scripts/run_e2e_macos.py --scenario core,files   # 选场景
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

ALL_SCENARIOS = sc.SCENARIOS + ["crash"]


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


def screenshot(path: Path):
    try:
        subprocess.run(["screencapture", "-x", str(path)], capture_output=True)
        console.step(f"现场截图: {path}")
    except OSError:
        pass


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="GraphStudio macOS 安装态 E2E")
    ap.add_argument("--app", default="", help=".app 路径（默认 dev 构建）")
    ap.add_argument("--scenario", default="all",
                    help=f"{','.join(ALL_SCENARIOS)} 组合，默认 all")
    ap.add_argument("--config", default="Release",
                    help="dev 构建配置（决定插件/库目录）")
    ap.add_argument("--artifacts", default="/tmp/gs_e2e_macos",
                    help="截图/临时文件输出目录")
    args = ap.parse_args()

    root = repo_root()
    artifacts = Path(args.artifacts)
    artifacts.mkdir(parents=True, exist_ok=True)
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

    # files/run 场景共用一张打开图（opencv_image_read 读生成的输入 png）
    graph_path = artifacts / "e2e_graph.json"
    input_png = artifacts / "e2e_input.png"
    input_png.write_bytes(tiny_png(64, 64))
    write_graph(graph_path, input_png)

    if not ensure_accessibility(prompt=True):
        console.fail("本终端没有辅助功能权限：系统设置 -> 隐私与安全性 -> "
                     "辅助功能，勾选运行本脚本的应用（终端/iTerm/VSCode…）")
        return 2

    results = {}
    main_names = [n for n in names if n != "crash"]
    need_launch = bool(main_names)

    if need_launch:
        console.step(f"启动 {binary}")
        env = launch_env(root, args.config)
        pid = os.posix_spawn(str(binary), [str(binary)], env)
        try:
            drv = MacDriver(pid)
            window = drv.window_by_title("Graph Studio", timeout=45)
            console.ok(f"已附着窗口（pid={pid}）")
            for name in main_names:
                console.step(f"场景 {name}")
                try:
                    if name == "core":
                        results[name] = sc.core(drv, window, artifacts)
                    elif name == "files":
                        results[name] = sc.files(drv, window, artifacts,
                                                 graph_path)
                    elif name == "run":
                        results[name] = sc.run_graph(drv, window, artifacts,
                                                     graph_path)
                    elif name == "lifecycle":
                        results[name] = sc.lifecycle(drv, window, artifacts)
                        status = wait_exit(pid, timeout=15)
                        if not (os.WIFEXITED(status)
                                and os.WEXITSTATUS(status) == 0):
                            raise sc.ScenarioError(
                                f"进程退出状态异常: {status}")
                    console.ok(results[name])
                except (sc.ScenarioError, ax_driver.AxDriverError,
                        TimeoutError, OSError) as e:
                    results[name] = None
                    console.fail(f"场景 {name} 失败: {e}")
                    screenshot(artifacts / f"{name}_fail.png")
        finally:
            if results.get("lifecycle") is None or "lifecycle" not in main_names:
                try:
                    os.kill(pid, 15)
                except ProcessLookupError:
                    pass

    # crash：独立进程跑 --test-crash，预期 SIGSEGV 非正常退出
    if "crash" in names:
        console.step("场景 crash（--test-crash，预期崩溃）")
        env = launch_env(root, args.config)
        pid = os.posix_spawn(str(binary), [str(binary), "--test-crash"], env)
        try:
            status = wait_exit(pid, timeout=30)
        except TimeoutError:
            results["crash"] = None
            os.kill(pid, 9)
            console.fail("crash 场景：进程未崩溃退出")
        else:
            crashed = os.WIFSIGNALED(status) and \
                os.WTERMSIG(status) in (11, 4)  # SIGSEGV / SIGTRAP
            if crashed:
                results["crash"] = "crash OK (预期 SIGSEGV 发生)"
                console.ok(results["crash"])
            else:
                results["crash"] = None
                console.fail(f"crash 场景：异常退出方式 status={status}")

    print()
    failed = [k for k, v in results.items() if v is None]
    if failed:
        console.fail(f"E2E 失败场景: {', '.join(failed)}（现场在 {artifacts}）")
        return 1
    console.ok(f"E2E 全部通过（{len(results)} 场景）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
