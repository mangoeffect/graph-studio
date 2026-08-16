"""msix.py — MSIX 包管理适配，全部经 PowerShell 子进程完成。

「真实安装」路径：信任签名证书（LocalMachine\\TrustedPeople）→ Add-AppxPackage →
Get-AppxPackage 查询 → Remove-AppxPackage 卸载。无管理员权限时可用 register_layout()
走 staging 目录 Register 模式（免签名免管理员，二进制与清单一致，但跳过签名校验）。
"""

from __future__ import annotations

import base64
import json
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path

from gs import console


class MsixError(RuntimeError):
    pass


@dataclass
class InstalledPackage:
    name: str
    pfn: str          # PackageFamilyName，如 GraphStudio_<hash>
    version: str      # 4 段 appx 版本
    install_location: Path
    full_name: str = ""   # PackageFullName（Remove-AppxPackage 需要，非 PFN）
    mode: str = "msix"  # msix=Add-AppxPackage 安装 / register=staging 注册

    @property
    def exe(self) -> Path:
        return self.install_location / "graph_studio.exe"

    @property
    def aumid(self) -> str:
        # AppxManifest: Application Id = "GraphStudio"
        return f"{self.pfn}!GraphStudio"

    @property
    def local_cache(self) -> Path:
        """MSIX 虚拟化重定向根（卸载时被一并删除）。"""
        return Path.home() / "AppData" / "Local" / "Packages" / self.pfn


def _esc(s: str) -> str:
    return str(s).replace("'", "''")


def run_ps(script: str, timeout: int = 180) -> tuple[int, str, str]:
    """用 -EncodedCommand 执行 PowerShell（规避引号转义问题），返回 (rc, stdout, stderr)。

    以字节捕获再解码：中文系统 PowerShell 控制台输出是 GBK，Python 默认按
    UTF-8 解码会抛 UnicodeDecodeError 丢掉全部输出（含真实错误信息）。
    数据解析只用 stdout——stderr 可能混入 CLIXML 进度记录等噪音。
    """
    script = ("[Console]::OutputEncoding=[Text.Encoding]::UTF8; " + script)
    encoded = base64.b64encode(script.encode("utf-16-le")).decode("ascii")
    try:
        proc = subprocess.run(
            ["powershell.exe", "-NoProfile", "-NonInteractive",
             "-ExecutionPolicy", "Bypass", "-EncodedCommand", encoded],
            capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        raise MsixError(f"PowerShell 执行超时（{timeout}s）")

    def _decode(raw: bytes) -> str:
        try:
            return raw.decode("utf-8")
        except UnicodeDecodeError:
            return raw.decode("gbk", errors="replace")

    return proc.returncode, _decode(proc.stdout or b""), _decode(proc.stderr or b"")


def get_installed(name: str = "GraphStudio") -> InstalledPackage | None:
    ps = ("$p = Get-AppxPackage -Name '%s'; "
          "if ($p) { @{name=$p.Name; pfn=$p.PackageFamilyName; fn=$p.PackageFullName; "
          "version=$p.Version; loc=$p.InstallLocation} | ConvertTo-Json -Compress }"
          % _esc(name))
    rc, out, _err = run_ps(ps)
    out = out.strip()
    if rc != 0 or not out:
        return None
    try:
        info = json.loads(out)
        return InstalledPackage(name=info["name"], pfn=info["pfn"],
                                version=info["version"],
                                install_location=Path(info["loc"]),
                                full_name=info.get("fn", ""))
    except (ValueError, KeyError) as e:
        raise MsixError(f"Get-AppxPackage 输出解析失败: {out!r} ({e})")


def wait_installed(name: str = "GraphStudio", timeout: float = 90) -> InstalledPackage:
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        last = get_installed(name)
        if last:
            return last
        time.sleep(2)
    raise MsixError(f"等待包 {name} 出现在 Get-AppxPackage 结果中超时")


def trust_cert(msix: Path) -> tuple[Path, str]:
    """导入签名证书，返回 (.cer 路径, 信任模式 localmachine/currentuser)。

    证书来源：包旁 build_msix.ps1 自动导出的 <Publisher>.cer；缺失则从包的
    Authenticode 签名里提取签名者证书（不依赖导出文件存在）。
    """
    cer = next((c for c in sorted(msix.parent.glob("*.cer"))), None)
    if cer is None:
        cer = msix.parent / (msix.stem + "_signer.cer")
        ps = ("$sig = Get-AuthenticodeSignature -FilePath '%s'; "
              "if (-not $sig.SignerCertificate) { Write-Error 'no signer certificate'; exit 2 } "
              "[IO.File]::WriteAllBytes('%s', $sig.SignerCertificate.Export('Cert'))"
              % (_esc(msix), _esc(cer)))
        rc, out, _err = run_ps(ps)
        if rc != 0:
            raise MsixError(f"从 MSIX 签名提取证书失败: {out.strip()}")
    ps = ("try { Import-Certificate -FilePath '%s' -CertStoreLocation "
          "Cert:\\LocalMachine\\TrustedPeople -ErrorAction Stop | Out-Null; 'LM' } "
          "catch { try { Import-Certificate -FilePath '%s' -CertStoreLocation "
          "Cert:\\CurrentUser\\TrustedPeople -ErrorAction Stop | Out-Null; 'CU' } "
          "catch { 'FAIL: ' + $_.Exception.Message } }" % (_esc(cer), _esc(cer)))
    rc, out, err = run_ps(ps)
    out = out.strip() or err.strip()
    if out.startswith("LM"):
        return cer, "localmachine"
    if out.startswith("CU"):
        console.warn("证书只能导入 CurrentUser\\TrustedPeople（无管理员权限）；"
                     "Add-AppxPackage 可能仍拒绝该包")
        return cer, "currentuser"
    raise MsixError(f"导入签名证书失败（需管理员写入 LocalMachine\\TrustedPeople）: "
                    f"{out}\n  提示：提权运行，或改用 --register 模式")


def install(msix: Path, allow_register_fallback: bool = False,
            staging_dir: Path | None = None) -> InstalledPackage:
    """Add-AppxPackage 安装（真实用户路径）。失败时按需降级 register 模式。"""
    uninstall_if_present()  # 干净状态
    ps = ("try { Add-AppxPackage -Path '%s' -ErrorAction Stop; 'OK' } "
          "catch { 'FAIL: ' + $_.Exception.Message }" % _esc(msix))
    rc, out, err = run_ps(ps, timeout=600)
    out = (out.strip() or err.strip())
    if out.startswith("OK"):
        pkg = wait_installed()
        console.ok(f"已安装 {pkg.name} {pkg.version} (msix) -> {pkg.install_location}")
        return pkg
    if allow_register_fallback and staging_dir and (staging_dir / "AppxManifest.xml").is_file():
        console.warn(f"Add-AppxPackage 失败，降级 register 模式: {out}")
        return register_layout(staging_dir)
    raise MsixError(f"Add-AppxPackage 失败: {out}")


def register_layout(staging_dir: Path) -> InstalledPackage:
    """免签名注册 staging 布局（Add-AppxPackage -Register），无需管理员。

    二进制/清单与打包产物一致，但不验证签名与包完整性——报告中需标注。
    """
    manifest = staging_dir / "AppxManifest.xml"
    if not manifest.is_file():
        raise MsixError(f"staging 布局缺失 AppxManifest.xml: {manifest}")
    uninstall_if_present()
    ps = ("try { Add-AppxPackage -Register '%s' -ErrorAction Stop; 'OK' } "
          "catch { 'FAIL: ' + $_.Exception.Message }" % _esc(manifest))
    rc, out, err = run_ps(ps, timeout=600)
    out = (out.strip() or err.strip())
    if not out.startswith("OK"):
        raise MsixError(f"Add-AppxPackage -Register 失败: {out}（开发模式是否启用？）")
    pkg = wait_installed()
    pkg.mode = "register"
    console.warn(f"已注册（register 模式，未验证签名）{pkg.version} -> {pkg.install_location}")
    return pkg


def update(msix: Path) -> InstalledPackage:
    """Add-AppxPackage 覆盖安装更高版本（同 identity）。"""
    ps = ("try { Add-AppxPackage -Path '%s' -ErrorAction Stop; 'OK' } "
          "catch { 'FAIL: ' + $_.Exception.Message }" % _esc(msix))
    rc, out, err = run_ps(ps, timeout=600)
    out = (out.strip() or err.strip())
    if not out.startswith("OK"):
        raise MsixError(f"覆盖安装（升级）失败: {out}")
    time.sleep(2)
    return wait_installed()


def uninstall_if_present(name: str = "GraphStudio") -> bool:
    pkg = get_installed(name)
    if not pkg:
        return False
    # Remove-AppxPackage 的 -Package 参数要 PackageFullName（含版本/架构），
    # 传 PackageFamilyName 会报“传递的参数无效”(0x80073CFA)。
    target = pkg.full_name or pkg.pfn
    ps = ("try { Remove-AppxPackage -Package '%s' -ErrorAction Stop; 'OK' } "
          "catch { 'FAIL: ' + $_.Exception.Message }" % _esc(target))
    rc, out, err = run_ps(ps, timeout=300)
    out = (out.strip() or err.strip())
    if not out.startswith("OK"):
        raise MsixError(f"Remove-AppxPackage 失败: {out.strip()}")
    deadline = time.time() + 60
    while time.time() < deadline:
        if get_installed(name) is None:
            console.ok(f"已卸载 {pkg.pfn}")
            return True
        time.sleep(2)
    raise MsixError("卸载后包仍存在（Get-AppxPackage 未清空）")


def launch_aumid(pkg: InstalledPackage) -> None:
    """经 shell:AppsFolder 激活（等价开始菜单启动）。explorer 立即返回。"""
    subprocess.Popen(["explorer.exe", f"shell:AppsFolder\\{pkg.aumid}"])


def kill_instances(pkg: InstalledPackage) -> int:
    """结束来自安装目录的 graph_studio 进程（不动开发构建树里的实例）。"""
    ps = ("Get-Process graph_studio -ErrorAction SilentlyContinue "
          "| Where-Object { $_.Path -eq '%s' } "
          "| ForEach-Object { Stop-Process -Id $_.Id -Force; $_.Id }" % _esc(pkg.exe))
    rc, out, _err = run_ps(ps, timeout=60)
    ids = [ln.strip() for ln in out.splitlines() if ln.strip().isdigit()]
    if ids:
        console.warn(f"结束了残留的安装态进程: {', '.join(ids)}")
    return len(ids)
