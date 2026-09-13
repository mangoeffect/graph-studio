# -*- coding: utf-8 -*-
"""adb.py — Android E2E 的 adb/设备封装（纯标准库，零第三方依赖）。

对齐 macOS E2E 的 ax_driver.py 定位：把"外部驱动"的脏活收在一处，场景层
（scenarios.py）只表达用例语义。设备侧没有常驻进程——每次执行是一次
`adb shell`（冷启动进程隔离，对齐 macOS 的 per-graph posix_spawn）。

进程手法（与本仓库既有 E2E 一致，run_e2e_macos.py 的 AppRunner 同款）：
  - os.posix_spawn 启动 adb，stdout/stderr 经 POSIX_SPAWN_DUP2 重定向到
    临时文件（不用管道：不会被缓冲区写满卡死），退出后一次读出；
  - 超时由 waitpid(WNOHANG) 轮询 + SIGKILL 兜底；
  - 设备命令一律以 **argv 列表** 传递，本模块从不拼接 shell 字符串
    （adb 把 `adb shell a b c` 的剩余参数原样交给设备 shell，每个 token 独立，
    无注入面）；
  - 不做设备侧输出重定向：runner 的 stdout 经 adb 管道回收后由主机侧写日志
    ——与 macOS/WASM 驱动"断言 [gs] 完成行"的通道一致，不依赖会话退出码语义。

adb 发现顺序：PATH -> $ANDROID_HOME/platform-tools -> $ANDROID_SDK_ROOT/
platform-tools -> macOS 默认 ~/Library/Android/sdk/platform-tools -> Linux
默认 ~/Android/Sdk/platform-tools（未装平台工具时给出可执行的错误提示）。

设备选择：--serial 显式指定；缺省要求 `adb devices` 恰好一台在线设备
（多设备时报错而不是猜——猜错会把测试跑在别人的设备上）。
"""

from __future__ import annotations

import os
import shutil
import tempfile
import time
from pathlib import Path


class AdbError(RuntimeError):
    pass


def find_adb() -> Path | None:
    """定位 adb 可执行文件（PATH -> ANDROID_HOME/ANDROID_SDK_ROOT -> 平台默认）。"""
    which = shutil.which("adb")
    if which:
        return Path(which)
    candidates = []
    for var in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        sdk = os.environ.get(var)
        if sdk:
            candidates.append(Path(sdk) / "platform-tools" / "adb")
    home = Path.home()
    candidates.append(home / "Library" / "Android" / "sdk" / "platform-tools" / "adb")  # macOS
    candidates.append(home / "Android" / "Sdk" / "platform-tools" / "adb")              # Linux
    for c in candidates:
        if c.is_file():
            return c
    return None


def _spawn_capture(argv: list[str], timeout: float) -> tuple[int, str]:
    """执行 argv，返回 (退出码, stdout+stderr 文本)。

    posix_spawn + DUP2 到匿名临时文件（无管道死锁风险），超时 SIGKILL。
    退出码经 waitstatus_to_exitcode 归一（信号终止返回负值）。
    """
    if not argv or not os.path.isabs(argv[0]) and not shutil.which(argv[0]):
        raise AdbError(f"可执行文件不存在: {argv[0] if argv else '(空)'}")
    with tempfile.TemporaryFile() as out:
        actions = [(os.POSIX_SPAWN_DUP2, out.fileno(), 1),
                   (os.POSIX_SPAWN_DUP2, out.fileno(), 2)]
        pid = os.posix_spawn(argv[0], argv, dict(os.environ), file_actions=actions)
        status = _wait_pid(pid, timeout)
        out.seek(0)
        text = out.read().decode("utf-8", "replace")
    code = os.waitstatus_to_exitcode(status) if status >= 0 else status
    return code, text


def _wait_pid(pid: int, timeout: float) -> int:
    """轮询等待子进程退出，返回 wait 状态；超时 SIGKILL 后仍等待（不留僵尸）。"""
    deadline = time.time() + timeout
    while True:
        done, status = os.waitpid(pid, os.WNOHANG)
        if done == pid:
            return status
        if time.time() >= deadline:
            try:
                os.kill(pid, 9)
            except ProcessLookupError:
                pass
            _, status = os.waitpid(pid, 0)
            return status
        time.sleep(0.05)


class AdbDevice:
    """一台（或一组候选中的）Android 设备的 adb 会话。"""

    def __init__(self, serial: str = "", adb: Path | None = None):
        self.adb = adb or find_adb()
        if not self.adb or not Path(self.adb).is_file():
            raise AdbError(
                "找不到 adb：装 Android platform-tools（brew install android-platform-tools）"
                "或设置 ANDROID_HOME=/path/to/android-sdk")
        self.serial = serial or self._pick_serial()

    # ---------- 会话基元 ----------

    def _invoke(self, argv: list[str], *, timeout: float,
                check: bool = True) -> tuple[int, str]:
        """执行 `adb [-s serial] <argv>`，返回 (退出码, 文本)。"""
        full = [str(self.adb)]
        if self.serial:
            full += ["-s", self.serial]
        full += argv
        code, text = _spawn_capture(full, timeout)
        if check and code != 0:
            raise AdbError(f"adb 失败（exit {code}），参数: {argv}\n{text.strip()}")
        return code, text

    @staticmethod
    def _pick_serial() -> str:
        adb = find_adb()
        if not adb:
            raise AdbError("找不到 adb（见 AdbDevice 文档）")
        _, text = _spawn_capture([str(adb), "devices"], 60.0)
        serials = []
        for line in text.splitlines()[1:]:
            parts = line.split()
            if len(parts) >= 2 and parts[1] == "device":
                serials.append(parts[0])
        if not serials:
            raise AdbError("没有在线设备：启动模拟器（emulator -avd <name>）或接 USB 真机"
                           "（adb devices 应显示 device）")
        if len(serials) > 1:
            raise AdbError(f"检测到多台设备 {serials}：用 --serial 指定（脚本不猜设备）")
        return serials[0]

    # ---------- 设备信息 ----------

    def getprop(self, prop: str) -> str:
        return self.shell(["getprop", prop], timeout=30).strip()

    def abi(self) -> str:
        return self.getprop("ro.product.cpu.abi")

    def fingerprint(self) -> str:
        """设备指纹（报告 meta 用；取不到返回空串而不是让用例失败）。"""
        try:
            return self.getprop("ro.build.fingerprint")
        except AdbError:
            return ""

    def sdk_level(self) -> str:
        return self.getprop("ro.build.version.sdk")

    # ---------- 文件与命令 ----------

    def shell(self, argv: list[str], *, timeout: float = 120.0,
              check: bool = True) -> str:
        """设备侧命令（argv 列表；adb 把每个 token 原样交给设备 shell）。"""
        return self._invoke(["shell", *argv], timeout=timeout, check=check)[1]

    def exec_remote(self, argv: list[str], *, timeout: float = 120.0) -> tuple[int, str]:
        """执行设备侧命令，返回 (退出码, stdout+stderr)。

        退出码取 adb 会话的返回码（adb shell 转发远端退出状态；runner 的完成
        断言以 [gs] 日志行为准，退出码只作辅助信号）。
        """
        return self._invoke(["shell", *argv], timeout=timeout, check=False)

    def push(self, local: Path, remote: str, *, timeout: float = 600.0) -> None:
        self._invoke(["push", str(local), remote], timeout=timeout)

    def pull(self, remote: str, local: Path, *, timeout: float = 600.0) -> None:
        local.parent.mkdir(parents=True, exist_ok=True)
        self._invoke(["pull", remote, str(local)], timeout=timeout)

    def mkdir(self, remote: str) -> None:
        self.shell(["mkdir", "-p", remote], timeout=30)

    def rm_rf(self, remote: str) -> None:
        self.shell(["rm", "-rf", remote], timeout=60, check=False)

    def chmod_exec(self, remote: str) -> None:
        """补执行位（adb push 的权限保留因 adb 版本/目标路径而异）。"""
        self.shell(["chmod", "755", remote], timeout=30)

    def pkill(self, pattern: str) -> None:
        """按进程名收尾（超时用例的兜底；设备侧无 SIGTERM 语义，直接 KILL）。"""
        self.shell(["pkill", "-9", "-f", pattern], timeout=30, check=False)

    def wait_device(self, timeout: float = 120.0) -> None:
        """等设备 boot 完成（cold boot 后 adb 早已在线但包管理器未就绪）。"""
        deadline = time.time() + timeout
        last = ""
        while time.time() < deadline:
            try:
                last = self.getprop("sys.boot_completed")
            except AdbError as e:
                last = str(e)
            if last.strip() == "1":
                return
            time.sleep(2.0)
        raise AdbError(
            f"设备 {self.serial} 未在 {timeout}s 内完成 boot（sys.boot_completed={last!r}）")
