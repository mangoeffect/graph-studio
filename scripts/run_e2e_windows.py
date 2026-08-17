#!/usr/bin/env python3
"""run_e2e_windows.py — GraphStudio Windows 安装态 E2E（真实环境、真实输入）。

对「用户拿到 MSIX → 安装 → 像真人一样使用 → 卸载」整条链路做黑盒自动化测试：
安装 build_msix.ps1 产出的签名包（或 --msix 指定现成包），从 shell:AppsFolder
启动安装态应用，用 pywinauto(UIA) + 真实鼠标键盘驱动，断言走 UIA 可见文本与
磁盘工件。场景与架构见 dev-docs/e2e-windows-installed.md。

前置:
  1. 交互式桌面会话（本地控制台即可；RDP 需保持连接）。
  2. pip install -r scripts/e2e_windows/requirements.txt（pywinauto、pillow）。
  3. 真实安装（Add-AppxPackage）需要管理员导入签名证书；非提权时自动降级
     staging register 模式（--register 强制）。

用法:
  python scripts/run_e2e_windows.py                    # 构建+安装+全场景+卸载
  python scripts/run_e2e_windows.py --msix <pkg.msix>  # 测现成包
  python scripts/run_e2e_windows.py --only core,files  # 场景过滤
  python scripts/run_e2e_windows.py --register         # 免管理员 register 模式

判定: 退出码 0 = 全部场景通过；1 = 有失败（现场在 dist/e2e/<时间戳>/）。
"""

from __future__ import annotations

import argparse
import ctypes
import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gs import console, repo_root  # noqa: E402

SCENARIOS = ["core", "files", "crash", "lifecycle"]


def is_admin() -> bool:
    try:
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:
        return False


def check_env() -> None:
    if os.name != "nt":
        console.fail("run_e2e_windows.py 仅支持 Windows")
        sys.exit(2)
    try:
        import pywinauto  # noqa: F401
        import PIL  # noqa: F401
    except ImportError:
        console.fail("缺少 E2E 依赖：python -m pip install -r "
                     "scripts/e2e_windows/requirements.txt")
        sys.exit(2)
    if os.environ.get("SESSIONNAME") == "Services":
        console.warn("当前处于 Session 0（服务会话），真实输入可能无效——"
                     "请在交互式桌面会话中运行（详见 dev-docs/e2e-windows-installed.md）")


def build_msix(args) -> Path:
    """调用 build_msix.ps1 构建签名测试包，返回包路径。"""
    console.step("构建签名 MSIX（build_msix.ps1）")
    script = repo_root() / "scripts" / "build_msix.ps1"
    cmd = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
           "-File", str(script), "-Config", args.config]
    if args.jobs:
        cmd += ["-Jobs", str(args.jobs)]
    if args.clean:
        cmd += ["-Clean"]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=3600)
    if proc.returncode != 0:
        console.fail(proc.stdout[-4000:] + proc.stderr[-2000:])
        sys.exit(1)
    out_dir = repo_root() / "dist" / "msix"
    pkgs = sorted(out_dir.glob("graph_studio-*.msix"), key=lambda p: p.stat().st_mtime)
    if not pkgs:
        console.fail(f"构建成功但 {out_dir} 下没有 graph_studio-*.msix")
        sys.exit(1)
    console.ok(f"测试包: {pkgs[-1]}")
    return pkgs[-1]


def main() -> int:
    console.init()
    ap = argparse.ArgumentParser(description="GraphStudio Windows 安装态 E2E")
    ap.add_argument("--msix", default="", help="测试现成 MSIX（默认现场构建）")
    ap.add_argument("--old-msix", default="", help="lifecycle：升级前版本包")
    ap.add_argument("--new-msix", default="", help="lifecycle：升级目标包")
    ap.add_argument("--only", default=",".join(SCENARIOS),
                    help=f"场景过滤，逗号分隔（默认全部）: {','.join(SCENARIOS)}")
    ap.add_argument("--max-graphs", type=int, default=10,
                    help="files 场景纳入的子模块图数量上限（默认 10，"
                         "read_image/unicode 优先）")
    ap.add_argument("--graphs", default="",
                    help="files 场景按路径子串过滤子模块图（如 gpu、mediapipe）")
    ap.add_argument("--config", default="RelWithDebInfo",
                    choices=["Debug", "Release", "RelWithDebInfo", "MinSizeRel"],
                    help="构建配置（默认 RelWithDebInfo）")
    ap.add_argument("-j", "--jobs", type=int, default=0, help="并行编译线程数")
    ap.add_argument("-c", "--clean", action="store_true", help="清空构建目录")
    ap.add_argument("--register", action="store_true",
                    help="强制 staging register 模式（免签名/免管理员）")
    ap.add_argument("--no-fallback", action="store_true",
                    help="真实安装失败时不降级 register 模式")
    ap.add_argument("--keep-installed", action="store_true", help="结束时保留安装")
    ap.add_argument("--keep-output", action="store_true", help="保留/打印中间信息")
    args = ap.parse_args()

    check_env()

    from e2e_windows import msix
    from e2e_windows.app import set_dpi_awareness
    from e2e_windows.fixtures import make_fixtures
    from e2e_windows.report import Report

    set_dpi_awareness()

    # MediaPipe 模型：files 场景的图资产（_stage_copy 从 submodule 树复制）与
    # build_msix 触发的 cmake configure（file(COPY) 进构建树）都要求模型已就位。
    # download_models 幂等（已存在即跳过）。缺模型时 mediapipe 场景只会被记
    # skip，而它们现在是 required——所以必须先下载。
    from run_all_submodules_test import download_models
    download_models(repo_root())

    msix_path = Path(args.msix) if args.msix else build_msix(args)
    report = Report(repo_root() / "dist" / "e2e")
    fixtures = make_fixtures(report.run_dir / "fixtures")
    console.step(f"E2E 运行目录: {report.run_dir}")

    # ---- 安装 ----
    staging = repo_root() / "dist" / "msix" / "staging"
    try:
        if args.register:
            pkg = msix.register_layout(staging)
        else:
            if not is_admin():
                console.warn("当前非管理员：证书只能入 CurrentUser 存储，"
                             "Add-AppxPackage 可能失败并降级 register 模式")
            msix.trust_cert(msix_path)
            pkg = msix.install(msix_path, allow_register_fallback=not args.no_fallback,
                               staging_dir=staging)
    except Exception as e:
        console.fail(f"安装失败: {e}")
        report.record("install", "fail", str(e))
        return report.finish()

    report.meta = {"msix": str(msix_path), "version": pkg.version,
                   "mode": pkg.mode, "pfn": pkg.pfn,
                   "admin": is_admin()}
    console.ok(f"被测包: {pkg.name} {pkg.version} ({pkg.mode}) @ {pkg.install_location}")

    ctx = {"msix_path": msix_path,
           "old_msix": Path(args.old_msix) if args.old_msix else None,
           "new_msix": Path(args.new_msix) if args.new_msix else None,
           "max_graphs": args.max_graphs,
           "graphs_filter": args.graphs}

    # ---- 场景 ----
    import e2e_windows.scenarios_core as core
    import e2e_windows.scenarios_files as files
    import e2e_windows.scenarios_crash as crash
    import e2e_windows.scenarios_lifecycle as lifecycle

    selected = [s.strip() for s in args.only.split(",") if s.strip()]
    for name in selected:
        if name not in SCENARIOS:
            report.record(name, "skip", f"未知场景（可选: {','.join(SCENARIOS)}）")
    mods = {"core": core, "files": files, "crash": crash, "lifecycle": lifecycle}
    for name in SCENARIOS:
        if name not in selected:
            report.record(name, "skip", "未选择")
            continue
        console.step(f"场景 [{name}]")
        try:
            mods[name].run(pkg, report, fixtures, ctx)
        except Exception as e:
            report.record(name, "fail", f"{type(e).__name__}: {e}", exc=e)
            continue
        except SystemExit as e:  # 场景内部不应退出进程，防御性记录
            report.record(name, "fail", f"SystemExit({e.code})")
            continue
        # 场景级状态聚合自子用例（core/xxx、files/open:xxx…）
        inner = [r for r in report.results if r.name.startswith(name + "/")]
        failed = [r for r in inner if r.status == "fail"]
        passed = [r for r in inner if r.status == "pass"]
        if failed:
            report.record(name, "fail",
                          f"{len(failed)}/{len(inner)} 子用例失败")
        else:
            report.record(name, "pass",
                          f"{len(passed)} 个子用例通过" if inner else "")

    # ---- 卸载（保留现场给 --keep-installed）----
    if not args.keep_installed:
        try:
            msix.uninstall_if_present()
        except Exception as e:
            report.record("uninstall", "fail", str(e))
        else:
            report.record("uninstall", "pass")

    return report.finish()


if __name__ == "__main__":
    sys.exit(main())
