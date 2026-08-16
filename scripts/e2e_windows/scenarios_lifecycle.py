"""scenarios_lifecycle.py — 覆盖安装升级 / 卸载清理。

前置：crash 场景在包作用域（LocalCache）留下了 *.dmp 工件；若没有则先触发一次。
升级：Add-AppxPackage 同 identity 更高版本（默认用 build_msix.ps1 -SkipBuild 以
更高版本号重打包当前产物，几分钟内完成；--old-msix/--new-msix 可显式指定）。
卸载：Remove-AppxPackage 后包、安装目录、LocalCache 全部消失。
"""

from __future__ import annotations

import re
import subprocess
import time
from pathlib import Path

from gs import console, repo_root

from . import msix
from .app import AppSession, wait_until
from . import scenarios_crash


def _bump(version: str) -> str:
    """0.1.0.0 → 0.1.1.0（第 3 段 +1，末段保持 0）。"""
    parts = version.split(".")
    parts[2] = str(int(parts[2]) + 1)
    return ".".join(parts)


def repackage_bumped(current_msix: Path, current_version: str, report) -> Path:
    """build_msix.ps1 -SkipBuild：同产物、更高版本号重打包+签名（不动构建树）。"""
    new_ver = _bump(current_version)
    out_dir = report.path("upgrade_pkg")
    ps_script = repo_root() / "scripts" / "build_msix.ps1"
    cmd = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
           "-File", str(ps_script), "-SkipBuild", "-SkipSentry",
           "-Version", new_ver, "-OutDir", str(out_dir)]
    console.step(f"重打包更高版本 {new_ver}（-SkipBuild）")
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=900)
    report.path("repackage.log").write_text(proc.stdout + proc.stderr,
                                            encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        raise RuntimeError(f"重打包失败（详见 repackage.log）: rc={proc.returncode}")
    pkg = next(iter(out_dir.glob("*.msix")), None)
    if not pkg:
        raise RuntimeError(f"重打包输出目录无 .msix: {out_dir}")
    return pkg


def run(pkg, report, fixtures, ctx) -> None:
    old_msix: Path = ctx.get("old_msix")
    new_msix: Path = ctx.get("new_msix")

    # 1) 确保有包作用域工件（crash 场景的 dump；或现场触发一次）
    artifact = ctx.get("crash_dump")
    if not artifact or not Path(artifact).exists():
        console.warn("无现成 crash dump 工件，现场触发一次")
        scenarios_crash.run(pkg, report, fixtures, ctx)
        artifact = ctx["crash_dump"]
    artifact = Path(artifact)
    console.ok(f"升级前工件: {artifact}")

    if old_msix and new_msix:
        # 显式双包模式：从旧包开始装
        msix.install(old_msix)
        pkg = msix.get_installed()
    elif new_msix:
        pkg_before = msix.get_installed()
        if not pkg_before:
            raise RuntimeError("--new-msix 需要当前已安装旧版本（或同时给 --old-msix）")
    else:
        pkg_before = msix.get_installed() or pkg
        new_msix = repackage_bumped(ctx["msix_path"], pkg_before.version, report)

    old_version = pkg.version
    console.step(f"覆盖安装升级: {old_version} -> {new_msix.name}")

    # 2) 升级
    pkg = msix.update(new_msix)
    if pkg.version == old_version:
        raise AssertionError(f"升级后版本未变化: {pkg.version}")
    console.ok(f"升级后版本 {pkg.version}")

    if not artifact.exists():
        # 真实 AppData 落点的 dump 不随包走，仅 LocalCache（包作用域）工件必须保留
        if ctx.get("crash_dump_package_scoped"):
            raise AssertionError(f"包作用域工件在升级后丢失: {artifact}")
        console.warn("工件不在包作用域（非虚拟化落点），跳过保留断言")
    else:
        console.ok("升级后用户工件保留")

    # 3) 升级后应用可正常启动
    s = AppSession(pkg, report)
    s.launch_and_attach()
    try:
        wait_until(lambda: s.status_counts(), desc="升级后窗口就绪")
        s.screenshot("upgrade_launch.png")
    finally:
        s.close()

    # 4) 卸载清理
    pfn = pkg.pfn
    install_dir = pkg.install_location
    cache_dir = pkg.local_cache
    msix.uninstall_if_present()

    if msix.get_installed() is not None:
        raise AssertionError("卸载后 Get-AppxPackage 仍能查到包")
    if install_dir.exists():
        raise AssertionError(f"卸载后安装目录仍存在: {install_dir}")
    console.ok(f"安装目录已清理: {install_dir}")

    deadline = time.time() + 30
    while time.time() < deadline and cache_dir.exists():
        time.sleep(2)
    if ctx.get("crash_dump_package_scoped") and cache_dir.exists():
        raise AssertionError(f"卸载后 LocalCache 仍存在: {cache_dir}")
    if cache_dir.exists():
        console.warn(f"LocalCache 未随卸载清理（工件非包作用域时属预期）: {cache_dir}")
    else:
        console.ok("LocalCache 已随卸载清理")
