# -*- coding: utf-8 -*-
"""session.py — MacSession：MacDriver 之上的用例级会话助手。

对齐 e2e_windows/app.py 的 AppSession 子集（status_counts / wait_finished /
run_and_wait / state_snapshot）。图文件的输入走 CLI 启动参数（--open/--run，
见 entry.cpp），不再驱动 NSOpenPanel——面板自动化（前往 sheet/懒加载行/
Open 按钮的时序竞态）极不稳定，教训记录在 dev-docs/e2e-macos.md。
主窗口标题随文件变化（"<file> - Graph Studio"），故每次都按标题重新定位，
不复用启动时拿到的窗口元素。
"""

from __future__ import annotations

import re
import time

from .ax_driver import (AxDriverError, MacDriver, kCGEventFlagMaskCommand,
                        kVK_ANSI_R)

FINISHED_RE = re.compile(r"Execution finished:\s*(\d+)\s*ok,\s*(\d+)\s*failed")
COUNTS_RE = re.compile(r"Nodes:\s*(\d+)\s*\|\s*Edges:\s*(\d+)")


class SessionError(RuntimeError):
    pass


def wait_finished_file(log_path, prior: int = 0,
                       timeout: float = 120.0) -> tuple[int, int]:
    """轮询 app 的 stderr 镜像日志，等第 prior+1 条 'Execution finished'。

    MainWindow::onLogMessage 有 qInfo "[gs]" 镜像进 stderr（两端 E2E 的
    统一断言通道），AppRunner 启动时把它 dup2 到文件。比 AX 读 Log Panel
    可靠：执行结束后底栏自动切到 Profile 页，隐藏的 Log 页 QPlainTextEdit
    会从 AX 树消失（onExecutionFinished -> setCurrentWidget(profilePanel_)）。
    """
    deadline = time.time() + timeout
    tail = ""
    while time.time() < deadline:
        try:
            text = log_path.read_text(errors="replace")
        except OSError:
            text = ""
        matches = FINISHED_RE.findall(text)
        if len(matches) > prior:
            ok, failed = matches[-1]
            return int(ok), int(failed)
        time.sleep(0.3)
        tail = text[-300:]
    raise SessionError(f"app 日志未出现 Execution finished（尾部: {tail!r}）")


class MacSession:
    def __init__(self, drv: MacDriver):
        self.drv = drv

    # ---- 主窗口定位（标题含文件名时也命中）----
    def main_window(self, timeout: float = 10.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            for w in self.drv.windows():
                try:
                    if w.role() == "AXWindow" and w.title().endswith("Graph Studio"):
                        return w
                except (AxDriverError, OSError):
                    continue
            time.sleep(0.3)
        raise SessionError("找不到主窗口（标题应以 Graph Studio 结尾）")

    def title(self) -> str:
        return self.main_window().title()

    def _wait_title(self, substr: str, timeout: float = 8.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if substr in self.title():
                return
            time.sleep(0.3)
        raise SessionError(f"窗口标题未包含 {substr!r}（当前 {self.title()!r}）")

    # ---- 状态栏计数 ----
    def status_counts(self) -> tuple[int, int]:
        window = self.main_window()
        el = self.drv.find_static_text_starting(window, "Nodes:", 10)
        if el is None:
            raise SessionError("找不到计数标签（Nodes: N | Edges: M）")
        m = COUNTS_RE.search(el.value_text())
        if not m:
            raise SessionError(f"计数标签解析失败: {el.value_text()!r}")
        return int(m.group(1)), int(m.group(2))

    def wait_counts(self, nodes=None, edges=None, timeout: float = 30.0):
        deadline = time.time() + timeout
        last = (-1, -1)
        while time.time() < deadline:
            last = self.status_counts()
            if (nodes is None or last[0] == nodes) and \
               (edges is None or last[1] == edges):
                return last
            time.sleep(0.3)
        raise SessionError(f"计数未达到 ({nodes}, {edges})，停在 {last}")

    # ---- 日志面板 ----
    def log_text(self) -> str:
        window = self.main_window()
        el = self.drv.find_by_description(window, "Log Panel", 10)
        if el is None:
            raise SessionError("找不到日志面板（AXDescription 'Log Panel'）")
        return el.value_text()

    # ---- 任务库 ----
    # 注意：Task Library（AXOutline）折叠态的叶子 task 类型不进 AX 树，
    # 不能用它探测插件可用性——未注册类型由 load 失败暴露。

    # ---- 菜单动作 ----
    def new_graph(self, attempts: int = 3):
        """File > New：清空画布。效果校验（计数归零 + 标题复位）+ 重试。"""
        last_err: Exception | None = None
        for _ in range(attempts):
            try:
                if not self.drv.menu_click("File", "New"):
                    raise SessionError("File > New 菜单未命中")
                self.wait_counts(0, 0, timeout=4)
                self._wait_title_reset()
                return
            except (SessionError, AxDriverError) as e:
                last_err = e
                time.sleep(0.3)
        raise SessionError(f"File > New 后画布未清空: {last_err}")

    def _wait_title_reset(self, timeout: float = 4.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.title() == "Graph Studio":
                return
            time.sleep(0.3)
        raise SessionError(f"标题未复位（当前 {self.title()!r}）")

    # ---- 执行 ----
    def wait_finished(self, prior: int = 0, timeout: float = 90.0) -> tuple[int, int]:
        """等待日志面板出现第 prior+1 条 'Execution finished'，返回 (ok, failed)。

        注意：执行结束后 MainWindow 会把底栏切到 Profile 页（AX 树随即剪掉
        隐藏的 Log 页），此方法只适合执行**前**读基准 + 交互观察；
        冷启动用例请用 wait_finished_file（stderr 镜像，不受 tab 切换影响）。
        """
        deadline = time.time() + timeout
        tail = ""
        while time.time() < deadline:
            matches = FINISHED_RE.findall(self.log_text())
            if len(matches) > prior:
                ok, failed = matches[-1]
                return int(ok), int(failed)
            time.sleep(0.5)
            tail = self.log_text()[-300:]
        raise SessionError(f"日志未出现新的 Execution finished"
                           f"（日志尾部: {tail!r}）")

    def run_and_wait(self, timeout: float = 90.0) -> tuple[int, int]:
        """Cmd+R 并等待本次执行的 Execution finished 行，返回 (ok, failed)。

        基准计数在点击**前**捕获；首击被偷走时小图会一直等满超时：
        重击一次再等半程。
        """
        prior = len(FINISHED_RE.findall(self.log_text()))
        for attempt in range(2):
            self.drv.key_combo(kVK_ANSI_R, kCGEventFlagMaskCommand)
            try:
                return self.wait_finished(
                    prior=prior, timeout=timeout if attempt == 0 else timeout / 2)
            except SessionError:
                if attempt == 1:
                    raise
        raise SessionError("unreachable")

    # ---- 失败现场 ----
    def state_snapshot(self) -> dict:
        snap = {"title": "", "nodes": None, "edges": None, "log_tail": ""}
        try:
            snap["title"] = self.title()
        except Exception:
            pass
        try:
            snap["nodes"], snap["edges"] = self.status_counts()
        except Exception:
            pass
        try:
            snap["log_tail"] = self.log_text()[-1500:]
        except Exception:
            pass
        return snap
