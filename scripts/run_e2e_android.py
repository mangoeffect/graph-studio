#!/usr/bin/env python3
"""run_e2e_android.py — Android 端 GraphStudio graph E2E（adb 冷启动 + [gs] 断言）。

定位（对齐 scripts/run_e2e_macos.py 与 run_e2e_wasm.py 的第三端）：
Android 侧仓库只产出 dist/android/ 静态库 SDK，没有 GraphStudio 的 app 形态
（无 Qt for Android 壳，见 dev-docs/e2e-android.md）。因此本轨道覆盖的是
"子模块单测图在 Android 上真实执行"——载体是 tests/android 的 tg_e2e_runner
（console 程序，与桌面 `graph_studio --open <json> --run` 同语义：每张图一个
干净进程），驱动经 adb 把它与 staging 后的图集推到 /data/local/tmp 逐图执行。

与 macOS 的机制对齐（非 GUI 部分逐项同构）：
  - 图发现/落位：共享 scripts/e2e_graph_cases.py（discover/stage_copy/裁剪）；
  - 完成断言：共享 [gs] 完成行契约（FINISHED_RE，三端同一正则）；
  - 报告：共享 scripts/e2e_report.py → dist/e2e_android/<时间戳>/；
  - 冷启动隔离：每图一次 adb shell（= macOS 的每图一个进程）；
  - 模块 skip：ANDROID_UNSUPPORTED_MODULES（= WASM 的 WASM_UNSUPPORTED_MODULES）。
不对齐的部分：GUI 编辑场景（macOS 的 core/lifecycle、WASM 的 core）与安装态
crash 上报——Android 无 app 可驱动；crash 场景退化为信号终止断言。

场景:
  boot   设备就绪 + runner ABI 匹配 + whole-archive 注册断言（--selfcheck）
  run    单图冒烟（生成的 tiny png 图 → 恰好 1 ok / 0 failed）
  files  全部可跑子模块单测图逐张冷启动执行（gpu/render/mediapipe/video_io skip）
  crash  --test-crash 预期 SIGSEGV

前置:
  - Android SDK platform-tools（adb；PATH / ANDROID_HOME / 平台默认路径均可）
  - 一台设备：arm64 模拟器（emulator -avd <name>）或 USB 真机；
    AVD 需与 runner ABI 一致（x86_64 模拟器用 build_android.py --also-x86-64）
  - runner：build_android_<abi>/tests/android/tg_e2e_runner；缺失时本脚本自动
    调用 scripts/build_android.py --e2e（需要 ANDROID_NDK / ANDROID_NDK_HOME）
  - OpenCV Android 预编译库：build_android/opencv/install（scripts/build_opencv_android.py）

用法:
  python scripts/run_e2e_android.py                       # 全部场景（默认 arm64 runner）
  python scripts/run_e2e_android.py --scenario boot,run   # 选场景
  python scripts/run_e2e_android.py --max-graphs 10       # files 裁剪到 10 张
  python scripts/run_e2e_android.py --graphs image_filter # files 按路径子串过滤
  python scripts/run_e2e_android.py --serial emulator-5554
  python scripts/run_e2e_android.py --no-build            # runner 缺失即报错，不自动构建
"""

import argparse
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, repo_root, runner as gs_runner  # noqa: E402
from e2e_android import scenarios as sc  # noqa: E402
from e2e_android.adb import AdbDevice, AdbError  # noqa: E402
from e2e_report import Report  # noqa: E402

ALL_SCENARIOS = sc.SCENARIOS

# 设备侧落位根（与主机 run_dir 内容对齐；shell 用户可读写，无需 root）
DEVICE_ROOT = "/data/local/tmp/gs_e2e"


def runner_path(root: Path, abi: str) -> Path:
    """runner 路径：优先推送用的去符号副本（带 -g 的二进制 137MB，模拟器 /data
    只有几百 MB；原文件留给崩溃符号化），没有则退回完整版。"""
    base = root / f"build_android_{abi}" / "tests" / "android"
    stripped = base / "tg_e2e_runner.stripped"
    return stripped if stripped.is_file() else base / "tg_e2e_runner"


def ensure_runner(root: Path, runner_bin: Path, abi: str, build: bool) -> bool:
    """runner 缺失时按需构建（build_android.py --e2e，幂等）。"""
    if runner_bin.is_file():
        return True
    if not build:
        console.fail(f"未找到 runner: {runner_bin}（先运行 "
                     f"python scripts/build_android.py --e2e --abi {abi}，"
                     f"或去掉 --no-build）")
        return False
    console.step(f"runner 缺失，构建中: python scripts/build_android.py --e2e --abi {abi}")
    code = gs_runner.check(
        [sys.executable, str(root / "scripts" / "build_android.py"),
         "--e2e", "--abi", abi],
        cwd=str(root), what=f"构建 Android e2e runner ({abi})")
    if code != 0 or not runner_bin.is_file():
        console.fail(f"runner 构建失败: {runner_bin}（若缺 NDK，先 export "
                     f"ANDROID_NDK=/path/to/ndk）")
        return False
    return True


def tiny_png(width: int, height: int) -> bytes:
    """确定性小图（与 run_e2e_macos.py 同源：同尺寸同像素公式）。"""
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


def write_graph(graph_path: Path, input_png_remote: str) -> None:
    """冒烟图：单任务 opencv_image_read，file_path 指向**设备侧**绝对路径
    （绝对路径不经 _source_dir 拼接，与 macOS 版写主机绝对路径同理）。"""
    graph_path.write_text(
        '{\n  "version": "2.0",\n  "tasks": [\n    { "id": "img", '
        '"type": "opencv_image_read",\n      "params": { "file_path": "'
        + input_png_remote + '" } }\n  ],\n  "edges": [],\n  "positions": '
        '{ "img": { "x": 0, "y": 0, "type": "opencv_image_read" } }\n}\n',
        encoding="utf-8")


def aggregate(report: Report, scenario: str):
    """场景级状态聚合自子用例（files/graph:xxx…），对齐 macOS/WASM 入口。"""
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
    ap = argparse.ArgumentParser(description="Android graph E2E（adb + tg_e2e_runner）")
    ap.add_argument("--scenario", default="all",
                    help=f"{','.join(ALL_SCENARIOS)} 组合，默认 all")
    ap.add_argument("--serial", default="",
                    help="adb 设备序列号（默认取唯一在线设备）")
    ap.add_argument("--runner", default="", help="runner 可执行路径（默认按 --abi 推导）")
    ap.add_argument("--abi", default="arm64-v8a",
                    help="runner 的 ABI（决定构建目录，默认 arm64-v8a）")
    ap.add_argument("--no-build", action="store_true",
                    help="runner 缺失时不自动构建")
    ap.add_argument("--artifacts", default="",
                    help="report 输出根目录（默认 <repo>/dist/e2e_android）")
    ap.add_argument("--max-graphs", type=int, default=0,
                    help="files 场景纳入的子模块图数量上限（默认 0=全部，"
                         "read_image/unicode 优先）")
    ap.add_argument("--graphs", default="",
                    help="files 场景按路径子串过滤子模块图（如 image_filtering）")
    args = ap.parse_args()

    names = (ALL_SCENARIOS if args.scenario == "all"
             else [s.strip() for s in args.scenario.split(",")])
    for n in names:
        if n not in ALL_SCENARIOS:
            console.fail(f"未知场景: {n}（可选 {', '.join(ALL_SCENARIOS)}）")
            return 1

    root = repo_root()
    runner_bin = Path(args.runner) if args.runner else runner_path(root, args.abi)

    # 设备（无 adb / 无设备 = 环境不就绪，退出码 2——对齐 macOS 的 TCC 未授权）
    try:
        device = AdbDevice(args.serial)
        device.wait_device(timeout=180)
    except AdbError as e:
        console.fail(f"设备不可用: {e}")
        return 2
    console.step(f"设备 {device.serial}（{device.abi()}, Android SDK {device.sdk_level()}）")

    if not ensure_runner(root, runner_bin, args.abi, not args.no_build):
        return 1

    report = Report(Path(args.artifacts) if args.artifacts
                    else root / "dist" / "e2e_android")
    report.meta = {
        "runner": str(runner_bin),
        "runner_abi": args.abi,
        "serial": device.serial,
        "device_abi": device.abi(),
        "device_sdk": device.sdk_level(),
        "device_fingerprint": device.fingerprint(),
        "scenarios": ",".join(names),
        "max_graphs": args.max_graphs or "all",
        "graphs_filter": args.graphs or "",
    }
    console.step(f"E2E 运行目录: {report.run_dir}")

    remote_root = f"{DEVICE_ROOT}/{report.run_dir.name}"
    runner = sc.DeviceRunner(device, runner_bin, report.run_dir, remote_root)

    # 设备侧清场：整个 gs_e2e 根（不止本次 stamp）——设备 /data 分区常只有几百
    # MB，历次运行的 runner 副本会把它塞满（实测 137MB × 2 即 "No space left"）
    device.rm_rf(DEVICE_ROOT)
    console.step(f"推送 runner -> {remote_root}")
    try:
        runner.prepare()
    except AdbError as e:
        console.fail(f"runner 推送失败: {e}")
        return 2

    # run 场景的冒烟图（生成的 tiny png 输入；图内引用设备侧绝对路径）
    graph_path = report.path("smoke/e2e_graph.json")
    input_png = report.path("smoke/e2e_input.png")
    input_png.write_bytes(tiny_png(64, 64))
    write_graph(graph_path, f"{remote_root}/smoke/e2e_input.png")
    # smoke/ 推上设备（files 场景会连同 submodule_graphs/ 再推一次，幂等）
    try:
        runner.push_run_dir()
    except AdbError as e:
        console.fail(f"落位推送失败: {e}")
        return 2

    ctx = {"max_graphs": args.max_graphs, "graphs_filter": args.graphs,
           "boot_timeout": 180}

    for name in names:
        console.step(f"场景 {name}")
        report.begin(name)
        try:
            if name == "boot":
                report.record(name, "pass", sc.boot(device, runner, ctx))
            elif name == "run":
                report.record(name, "pass", sc.run_graph(runner, ctx))
            elif name == "files":
                count = sc.files_run(runner, report, ctx)
                aggregate(report, "files")
                console.step(f"files: 执行 {count} 张图")
            elif name == "crash":
                report.record(name, "pass", sc.crash(runner, ctx))
        except Exception as e:   # 用例级兜底：意外异常也记录后继续
            report.record(name, "fail", f"{type(e).__name__}: {e}",
                          exc=e, app_state=getattr(e, "snapshot", None))

    return report.finish()


if __name__ == "__main__":
    sys.exit(main())
