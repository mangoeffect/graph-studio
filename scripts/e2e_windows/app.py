"""app.py — 安装态 GraphStudio 的 AppSession：启动/附加、UIA 查询、真实输入注入。

关键几何常量来自 NodeItem.cpp（节点 140×70、坐标系以节点中心为原点、单端口节点
端口在中心 ±70px、命中半径 14px）。右键菜单创建节点时「右键点 = 节点中心」
（GraphScene::contextMenuEvent 直接用右键 scenePos 建节点），据此做屏幕坐标推算。

输入注入约定:
  - 定位用 SetCursorPos（OS 按调用进程 DPI 上下文换算，与 UIA 矩形同空间）；
    按键用 mouse_event（真实输入事件）。
  - 所有按键事件前有「光标守卫」：定位后回读校验，被外部进程/用户抢走时
    微秒级重试（开发机上实测有后台程序持续重设光标）。
"""

from __future__ import annotations

import ctypes
import ctypes.wintypes
import os
import re
import struct
import subprocess
import time
from pathlib import Path

from pywinauto import Desktop, Application
from pywinauto.keyboard import send_keys

from gs import console
from . import msix

NODE_W = 140
PORT_DX = NODE_W // 2          # 单端口节点：输出端口在中心 +70，输入端口在 -70
WM_DROPFILES = 0x0233
GMEM_MOVEABLE = 0x0002
CF_UNICODETEXT = 13

_user32 = ctypes.windll.user32
_kernel32 = ctypes.windll.kernel32

MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP = 0x0002, 0x0004
MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP = 0x0008, 0x0010

# x64 下指针型 API 必须显式声明 restype/argtypes：默认 c_int 会把 64 位
# 指针截断成 32 位再符号扩展，后续 memmove 直接访问违例（0xFFFFFFFF…）。
_kernel32.GlobalAlloc.restype = ctypes.c_void_p
_kernel32.GlobalAlloc.argtypes = [ctypes.c_uint, ctypes.c_size_t]
_kernel32.GlobalLock.restype = ctypes.c_void_p
_kernel32.GlobalLock.argtypes = [ctypes.c_void_p]
_kernel32.GlobalUnlock.argtypes = [ctypes.c_void_p]
_user32.SetClipboardData.argtypes = [ctypes.c_uint, ctypes.c_void_p]
_user32.SendMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint,
                                 ctypes.c_void_p, ctypes.c_void_p]
_user32.SendMessageW.restype = ctypes.c_ssize_t

FINISHED_RE = re.compile(r"Execution finished:\s*(\d+)\s*ok,\s*(\d+)\s*failed")
PROFILE_RE = re.compile(
    r"tasks\s*\((\d+)\s*ok,\s*(\d+)\s*failed,\s*(\d+)\s*skipped\)")

# File 菜单固定布局（CreateMenuBar 顺序）→ 键盘快捷路径（ALT+F 等）。
# 真实鼠标点菜单栏的暴露窗口有数秒（点击+搜索+点击），期间光标/焦点被外部
# 干扰抢走即失败；键盘路径（mnemonic + DOWN + ENTER）亚秒完成，不依赖 UIA 搜索。
MENU_SHORTCUTS = {
    ("File", "New"): ("%f", 0),
    ("File", "Open..."): ("%f", 1),
    ("File", "Save"): ("%f", 2),
    ("File", "Save As..."): ("%f", 3),
    ("File", "Exit"): ("%f", 4),
}


class AppError(RuntimeError):
    pass


# ---------------- 输入原语 ----------------

def _set_pos(x: int, y: int) -> bool:
    """定位光标并回读校验。返回是否落在目标 ±2px 内。"""
    _user32.SetCursorPos(x, y)
    pt = ctypes.wintypes.POINT()
    _user32.GetCursorPos(ctypes.byref(pt))
    return abs(pt.x - x) <= 2 and abs(pt.y - y) <= 2


def _guarded_pos(x: int, y: int, attempts: int = 40) -> None:
    """带光标守卫的定位：被外部抢走（用户动鼠标/后台软件重设）则快速重试。

    干扰观测周期约 100ms；本循环每轮 ~5ms，通常能在稳定窗口内就位，
    随后立即发出按键事件（按键落在当时光标位置，窗口期 <1ms）。
    """
    for _ in range(attempts):
        if _set_pos(x, y):
            return
        time.sleep(0.005)
    console.warn(f"光标守卫：{attempts} 次定位均被抢走，仍发送事件于 ({x},{y})")


def real_click(x: int, y: int, button: str = "left"):
    """真实点击：守卫定位 + button down/up。

    按下与释放前各守卫一次——Qt 的 context menu 在右键**释放**时的光标位置
    触发（WM_CONTEXTMENU 语义），按下后光标被外部抢走会让菜单弹在别的控件上。
    按钮按下期间 Qt 已捕获鼠标，把光标拉回原位再释放即可保证落点正确。
    """
    _guarded_pos(x, y)
    time.sleep(0.03)
    down, up = ((MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP) if button == "right"
                else (MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP))
    _user32.mouse_event(down, 0, 0, 0, 0)
    time.sleep(0.05)
    _guarded_pos(x, y)
    time.sleep(0.02)
    _user32.mouse_event(up, 0, 0, 0, 0)
    time.sleep(0.05)


def real_press(x: int, y: int):
    _guarded_pos(x, y)
    time.sleep(0.04)
    _user32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)


def real_release(x: int, y: int):
    _guarded_pos(x, y)
    time.sleep(0.02)
    _user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)


def real_move(x: int, y: int, steps: int = 3, delay: float = 0.03):
    """分步移动：每步 SetCursorPos 都会向光标下窗口投递 WM_MOUSEMOVE，
    足以驱动 QMenu hover 与 QGraphicsView 拖拽跟踪。"""
    pt = ctypes.wintypes.POINT()
    _user32.GetCursorPos(ctypes.byref(pt))
    x0, y0 = pt.x, pt.y
    for i in range(1, steps + 1):
        t = i / steps
        _set_pos(int(x0 + (x - x0) * t), int(y0 + (y - y0) * t))
        time.sleep(delay)


# ---------------- 环境/工具 ----------------

def set_dpi_awareness() -> None:
    """harness 进程设为 Per-Monitor V2 DPI 感知：坐标统一物理像素。"""
    try:
        _user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
    except Exception:
        pass


def exe_path_of_pid(pid: int) -> str | None:
    h = _kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not h:
        return None
    try:
        buf = ctypes.create_unicode_buffer(1024)
        size = ctypes.c_ulong(1024)
        if _kernel32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(size)):
            return buf.value
        return None
    finally:
        _kernel32.CloseHandle(h)


def set_clipboard_text(text: str) -> None:
    """经剪贴板输入文本（规避 send_keys 的 +^%~(){} 转义问题，也是真实用户做法）。"""
    for _ in range(10):
        if _user32.OpenClipboard(0):
            break
        time.sleep(0.05)
    else:
        raise AppError("OpenClipboard 失败")
    try:
        _user32.EmptyClipboard()
        data = text.encode("utf-16-le") + b"\x00\x00"
        h = _kernel32.GlobalAlloc(GMEM_MOVEABLE, len(data))
        if not h:
            raise AppError("GlobalAlloc 失败")
        p = _kernel32.GlobalLock(h)
        if not p:
            raise AppError("GlobalLock 失败")
        ctypes.memmove(p, data, len(data))
        _kernel32.GlobalUnlock(h)
        if not _user32.SetClipboardData(CF_UNICODETEXT, h):
            raise AppError("SetClipboardData 失败")
    finally:
        _user32.CloseClipboard()


def wait_until(pred, timeout: float = 10.0, interval: float = 0.25, desc: str = ""):
    """轮询等待谓言为真，超时抛 AppError。"""
    deadline = time.time() + timeout
    last_err = None
    while time.time() < deadline:
        try:
            v = pred()
            if v:
                return v
        except Exception as e:  # UIA 偶发 COM 错误：继续轮询
            last_err = e
        time.sleep(interval)
    suffix = f"（最后错误: {last_err}）" if last_err else ""
    raise AppError(f"等待超时（{timeout}s）: {desc or pred}{suffix}")


def _mid(rect) -> tuple[int, int]:
    return (rect.left + (rect.right - rect.left) // 2,
            rect.top + (rect.bottom - rect.top) // 2)


GW_OWNER = 4


def _enum_windows() -> list[int]:
    """Win32 EnumWindows（比 UIA 根枚举可靠——MSIX 宿主进程分裂时 UIA 树查不到
    对话框窗口）。回调必须保活到 EnumWindows 返回。"""
    out: list[int] = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def cb(hwnd, _lparam):
        out.append(hwnd)
        return True

    _user32.EnumWindows(cb, 0)
    return out


def _win_class(hwnd) -> str:
    buf = ctypes.create_unicode_buffer(256)
    _user32.GetClassNameW(hwnd, buf, 256)
    return buf.value


def _win_title(hwnd) -> str:
    buf = ctypes.create_unicode_buffer(256)
    _user32.GetWindowTextW(hwnd, buf, 256)
    return buf.value


# ---------------- AppSession ----------------

def escape_keys(text: str) -> str:
    """转义 pywinauto send_keys 的特殊字符（+^%~(){}[]），路径/数值可直接键入。"""
    out = []
    for ch in str(text):
        out.append("{" + ch + "}" if ch in "+^%~(){}[]" else ch)
    return "".join(out)


class AppSession:
    """一次安装态会话：shell 激活启动 + UIA 附加 + 真实输入驱动。"""

    def __init__(self, pkg: msix.InstalledPackage, report):
        self.pkg = pkg
        self.report = report
        self.app: Application | None = None
        self.win = None
        self.pid: int | None = None

    # ---------- 启动 / 退出 ----------
    def _find_window(self):
        for w in Desktop(backend="uia").windows():
            try:
                if "Graph Studio" not in w.window_text():
                    continue
                p = exe_path_of_pid(w.process_id())
                if p and Path(p).resolve() == self.pkg.exe.resolve():
                    return w
            except Exception:
                continue
        return None

    def launch_and_attach(self, timeout: float = 45.0):
        msix.kill_instances(self.pkg)
        msix.launch_aumid(self.pkg)
        w = wait_until(self._find_window, timeout=timeout, desc="主窗口出现")
        self.pid = w.process_id()
        self.app = Application(backend="uia").connect(process=self.pid)
        self.win = self.app.window(handle=w.handle)
        self.win.wait("ready", timeout=20)
        self.normalize_window()
        return self

    def normalize_window(self):
        try:
            hwnd = self.win.handle
            if _user32.IsZoomed(hwnd):
                _user32.ShowWindow(hwnd, 9)  # SW_RESTORE
                time.sleep(0.2)
            screen_w = _user32.GetSystemMetrics(0)
            screen_h = _user32.GetSystemMetrics(1)
            width, height = min(1440, screen_w - 80), min(900, screen_h - 80)
            # UIA 后端没有 move_window；直接 SetWindowPos（物理像素，DPI 已感知）
            _user32.SetWindowPos(hwnd, 0, 40, 30, width, height, 0x0004)  # SWP_NOZORDER
            time.sleep(0.4)
            self.win.set_focus()
            time.sleep(0.3)
        except Exception as e:
            console.warn(f"规整窗口几何失败（继续）: {e}")

    def close(self, timeout: float = 10.0):
        if not self.pid:
            return
        try:
            self.win.close()
        except Exception:
            pass
        deadline = time.time() + timeout
        while time.time() < deadline:
            if exe_path_of_pid(self.pid) is None:
                return
            time.sleep(0.5)
        subprocess.run(["taskkill", "/PID", str(self.pid), "/F"],
                       capture_output=True)

    # ---------- 现场采集 ----------
    def screenshot(self, name: str) -> str:
        return self.report.screenshot(self.win.capture_as_image(), name)

    def dump_ui(self, name: str):
        p = self.report.path(name)
        try:
            self.win.print_control_identifiers(filename=str(p))
        except Exception as e:
            p.write_text(f"UIA 树转储失败: {e}", encoding="utf-8")

    def state_snapshot(self) -> dict:
        """失败诊断用：当前 UI 可见状态一览（进 report）。"""
        snap = {"title": "", "nodes": None, "edges": None,
                "result_items": [], "run_enabled": None, "log_tail": ""}
        try:
            snap["title"] = self.win.window_text()
        except Exception:
            pass
        try:
            snap["nodes"], snap["edges"] = self.status_counts()
        except Exception:
            pass
        try:
            snap["result_items"] = self.result_combo_texts()
        except Exception:
            pass
        try:
            snap["run_enabled"] = self.run_enabled()
        except Exception:
            pass
        try:
            self.ensure_log_visible()   # Profile 页激活时 Log 隐藏、Name 为空
            log = self.log_text()
            snap["log_tail"] = "\n".join(log.splitlines()[-30:])
        except Exception:
            pass
        return snap

    # ---------- 画布几何 ----------
    def canvas_rect(self):
        el = wait_until(lambda: self._by_name("Graph Canvas"), desc="画布控件")
        return el.rectangle()

    def canvas_center(self) -> tuple[int, int]:
        return _mid(self.canvas_rect())

    def _by_name(self, name, control_type=None):
        for el in self.win.descendants(control_type=control_type):
            if el.window_text() == name:
                return el
        return None

    # ---------- UIA 查询（断言通道） ----------
    def status_counts(self) -> tuple[int, int]:
        texts = " | ".join(t.window_text()
                           for t in self.win.descendants(control_type="Text"))
        m = re.search(r"Nodes:\s*(\d+)\s*\|\s*Edges:\s*(\d+)", texts)
        if not m:
            raise AppError(f"状态栏未找到 Nodes/Edges 文本: {texts[:200]!r}")
        return int(m.group(1)), int(m.group(2))

    def task_library_texts(self) -> list[str]:
        tree = self._by_name("Task Library", control_type="Tree")
        if tree is None:
            trees = self.win.descendants(control_type="Tree")
            tree = trees[0] if trees else None
        if tree is None:
            return []
        return [it.window_text() for it in tree.descendants(control_type="TreeItem")]

    def result_combo_texts(self) -> list[str]:
        """结果下拉条目。未展开的 Qt ComboBox 在 UIA 里枚举不到 ListItem——
        像真实用户一样点开下拉再读，读完 ESC 收起。"""
        combo = self._by_name("Image Results", control_type="ComboBox")
        if combo is None:
            combos = self.win.descendants(control_type="ComboBox")
            combo = combos[0] if combos else None
        if combo is None:
            return []
        try:
            if not combo.is_enabled():
                return []
        except Exception:
            return []
        real_click(*_mid(combo.rectangle()))
        time.sleep(0.3)
        items = [it.window_text() for it in combo.descendants(control_type="ListItem")]
        send_keys("{ESC}")
        time.sleep(0.15)
        return items

    def selected_node_id(self) -> str:
        group = self._by_name("Selected Node", control_type="Group")
        if group is None:
            raise AppError("属性面板 'Selected Node' 分组未找到")
        edits = group.descendants(control_type="Edit")
        if not edits:
            raise AppError("'Selected Node' 分组内无 Edit")
        return edits[0].window_text()

    def _panel_text(self, marker: str) -> str:
        """按内容特征找面板编辑框——Qt 的 QPlainTextEdit 在 UIA 里 Name 就是全文，
        accessibleName（'Log Panel' 等）不生效。"""
        for el in self.win.descendants(control_type="Edit"):
            try:
                txt = el.window_text() or ""
            except Exception:
                continue
            if marker in txt:
                return txt
        return ""

    def log_text(self) -> str:
        return self._panel_text("[INFO]")

    def output_text(self) -> str:
        for el in self.win.descendants(control_type="Edit"):
            try:
                txt = el.window_text() or ""
            except Exception:
                continue
            if "Waiting for execution" in txt or "Executing" in txt:
                return txt
        return ""

    def finished_counts(self, log: str | None = None) -> int:
        """日志中 'Execution finished: N ok, M failed' 出现次数（跨多次 Run 累计）。"""
        return len(FINISHED_RE.findall(log if log is not None else self.log_text()))

    # ---------- 菜单 / 工具栏 ----------
    def _menu_item_in(self, window, name: str):
        try:
            for it in window.descendants(control_type="MenuItem"):
                if it.window_text() == name and it.is_enabled():
                    return it
        except Exception:
            pass
        return None

    def _popup_menu_item(self, name: str):
        """找弹出菜单项。

        优先查主窗口子树（菜单栏下拉项挂在主窗口 UIA 树里），再只扫同 pid 的
        QMenu 顶层窗口（右键菜单及其子菜单都挂在 QMenu 窗口里）——避免对整个
        桌面做 descendants（每轮数秒，抗不住轮询超时）。
        """
        it = self._menu_item_in(self.win, name)
        if it:
            return it
        for w in Desktop(backend="uia").windows():
            try:
                if self.pid and w.process_id() != self.pid:
                    continue
                if w.class_name() != "QMenu":
                    continue
            except Exception:
                continue
            it = self._menu_item_in(w, name)
            if it:
                return it
        return None

    def _menu_texts(self, category: str) -> list[str] | None:
        """顶层 QMenu 的菜单项文本（用于计算键盘导航的 DOWN 次数）。"""
        for w in Desktop(backend="uia").windows():
            try:
                if w.process_id() != self.pid or w.class_name() != "QMenu":
                    continue
                texts = [it.window_text()
                         for it in w.descendants(control_type="MenuItem")]
                if category in texts:
                    return texts
            except Exception:
                continue
        return None

    def menu_click(self, *path: str, attempts: int = 3):
        """点击主菜单（如 menu_click('File', 'Open...')）。

        已知路径优先走键盘快捷（MENU_SHORTCUTS：ALT+mnemonic + DOWN + ENTER，
        抗光标干扰）；每一步带效果验证：mnemonic 后确认下拉真的打开（目标项
        进入主窗口子树——菜单栏常态只有 File/Edit/... 顶级项），没打开则
        ESC 清场重来（此前版本的和弦可能被焦点抖动整个吃掉且静默失败）。
        未知路径退回鼠标方案（带重试）。
        """
        sc = MENU_SHORTCUTS.get(tuple(path))
        if sc is not None:
            chord, idx = sc
            last_err: Exception | None = None
            for _attempt in range(attempts):
                try:
                    self.win.set_focus()
                    send_keys(chord)          # 打开下拉（mnemonic 会预选第一项）
                    time.sleep(0.2)
                    # 验证下拉已打开：目标项只在下拉展开时出现在主窗口子树
                    wait_until(lambda: self._menu_item_in(self.win, path[-1]),
                               timeout=2, interval=0.15,
                               desc=f"下拉已打开（含 {path[-1]}）")
                    if idx > 0:
                        send_keys("{DOWN %d}" % idx)   # 预选 idx0：选 idx 只需 idx 次
                        time.sleep(0.12)
                    send_keys("{ENTER}")
                    time.sleep(0.15)
                    return
                except AppError as e:
                    last_err = e
                    send_keys("{ESC}")
                    time.sleep(0.25)
            raise last_err
        last_err = None
        for attempt in range(attempts):
            try:
                top = wait_until(lambda: self._menu_item_in(self.win, path[0]),
                                 desc=f"菜单栏项 {path[0]}")
                self.win.set_focus()
                real_click(*_mid(top.rectangle()))
                for label in path[1:]:
                    item = wait_until(lambda: self._popup_menu_item(label),
                                      timeout=8, interval=0.3,
                                      desc=f"弹出菜单项 {label}")
                    time.sleep(0.1)
                    real_click(*_mid(item.rectangle()))
                return
            except AppError as e:
                last_err = e
                send_keys("{ESC}")
                time.sleep(0.3)
        raise last_err

    def toolbar_button(self, name: str):
        for b in self.win.descendants(control_type="Button"):
            if b.window_text() == name:
                return b
        return None

    def run_button(self):
        return wait_until(lambda: self.toolbar_button("Run"), desc="Run 按钮")

    def click_run(self):
        real_click(*_mid(self.run_button().rectangle()))

    def run_enabled(self) -> bool:
        b = self.toolbar_button("Run")
        return bool(b and b.is_enabled())

    def new_graph(self):
        """File > New：清空画布（vm_.clear，无确认弹窗）。效果校验 + 重试。"""
        for attempt in range(3):
            self.menu_click("File", "New")
            try:
                wait_until(lambda: self.status_counts() == (0, 0),
                           timeout=4, desc="New 后画布清空")
                wait_until(lambda: self.win.window_text() == "Graph Studio",
                           timeout=4, desc="New 后标题复位")
                return
            except AppError:
                if attempt == 2:
                    raise
                time.sleep(0.3)

    # ---------- 节点操作（真实鼠标） ----------
    def add_node_at(self, task_type: str, screen_pt: tuple[int, int], category: str,
                    attempts: int = 3):
        """右键画布 → 分类子菜单 → 任务类型。右键点即新节点中心（屏幕坐标）。

        子菜单用键盘导航（DOWN×(idx+1) + RIGHT）打开；整段（右键→导航→点
        叶子）带重试——光标/焦点干扰可能打断任一步，ESC 清场后重来。
        """
        last_err: Exception | None = None
        for attempt in range(attempts):
            try:
                self.win.set_focus()
                real_click(screen_pt[0], screen_pt[1], button="right")
                texts = wait_until(lambda: self._menu_texts(category),
                                   timeout=5, interval=0.3,
                                   desc=f"右键菜单（含分类 {category}）")
                idx = texts.index(category)
                # 菜单初始无选中：第 1 次 DOWN 选中 idx 0，故选中 idx 需 idx+1 次
                send_keys("{DOWN %d}" % (idx + 1))
                time.sleep(0.15)
                send_keys("{RIGHT}")
                leaf = wait_until(lambda: self._popup_menu_item(task_type),
                                  timeout=6, interval=0.3,
                                  desc=f"子菜单任务 {task_type}")
                real_click(*_mid(leaf.rectangle()))
                time.sleep(0.2)
                return
            except AppError as e:
                last_err = e
                send_keys("{ESC}")
                time.sleep(0.25)
        raise last_err

    def connect_ports(self, src_center: tuple[int, int], dst_center: tuple[int, int]):
        """输出端口(中心+70) → 输入端口(中心-70) 的真实鼠标拖拽连线。

        移动必须用真实输入（mouse_event/SendInput 级别）——SetCursorPos 不产生
        WM_MOUSEMOVE，QGraphicsView 的拖拽跟踪收不到 move 事件。
        """
        out_pt = (src_center[0] + PORT_DX, src_center[1])
        in_pt = (dst_center[0] - PORT_DX, dst_center[1])
        real_press(*out_pt)
        real_move(in_pt[0], in_pt[1], steps=10, delay=0.04)
        time.sleep(0.1)
        real_release(*in_pt)
        time.sleep(0.2)

    def select_node(self, center: tuple[int, int]):
        real_click(center[0], center[1])  # 节点体中心（端口在 ±70px，安全）
        time.sleep(0.3)

    # ---------- 参数面板 ----------
    def _param_widget(self, label: str):
        """Parameters 分组内按行标签找参数控件。

        QFormLayout 行 = Text 标签 + 控件；file/int-with-slider 等参数的控件
        会被 QWidget 容器包裹（容器内才是真控件），因此从标签向后扫到下一行
        标签为止，收集区间内所有 Edit/Spinner。QSpinBox/QDoubleSpinBox 在 UIA
        里是 Spinner（内含 Edit）。返回可直接输入的 Edit。
        """
        group = self._by_name("Parameters", control_type="Group")
        if group is None:
            return None
        children = group.descendants()
        label_idx = None
        for i, el in enumerate(children):
            if el.element_info.control_type == "Text":
                txt = (el.window_text() or "").strip().rstrip(":")
                if txt == label:
                    label_idx = i
                    break
        if label_idx is None:
            return None
        # 行区间 = 标签之后到下一个行标签之前的全部后代（含容器内嵌控件）
        for el in children[label_idx + 1:]:
            kind = el.element_info.control_type
            if kind == "Text":
                break               # 到达下一行：本行没有可输入控件
            if kind == "Edit":
                return el
            if kind == "Spinner":
                edits = el.descendants(control_type="Edit")
                if edits:
                    return edits[0]
                return el
            if kind in ("ComboBox", "CheckBox"):
                return None         # 当前用例不涉及；显式不支持
            # Custom/Pane/Group 容器：继续向后扫（内嵌控件已在扁平列表里）
        return None

    def set_param_value(self, label: str, value: str):
        """在 Parameters 分组按标签输入值（点击→全选→粘贴→回车提交）。

        QLineEdit（string/file）、QSpinBox（int）、QDoubleSpinBox（float）通用；
        editingFinished → ChangeParamCommand（可撤销，与真人输入同路径）。
        提交后回读校验——真实输入可能被外部光标/焦点干扰偷走，参数没落上
        会直接导致后续执行空路径失败（实测发生），必须当场发现重试。
        """
        edit = wait_until(lambda: self._param_widget(label),
                          timeout=8, interval=0.3,
                          desc=f"参数控件 {label}")
        for attempt in range(3):
            real_click(*_mid(edit.rectangle()))
            send_keys("^a")
            set_clipboard_text(str(value))
            send_keys("^v")
            time.sleep(0.15)
            send_keys("{ENTER}")
            time.sleep(0.12)
            try:
                # 回读双通道：QSpinBox 内嵌 Edit 的 UIA Name 可能陈旧（实测
                # 提交成功仍读到旧值），LegacyIAccessible Value 是权威通道
                current = edit.window_text() or ""
                try:
                    current += " " + str(edit.legacy_properties().get("Value", ""))
                except Exception:
                    pass
            except Exception:
                current = ""
            if str(value) in current:
                return
            if attempt == 2:
                raise AppError(f"参数 {label} 提交校验失败: 期望 {value!r} "
                               f"实际 {current!r}")

    def defocus_to_canvas(self):
        real_click(*self.canvas_center())  # 点画布空白去焦点（促 editingFinished）
        time.sleep(0.2)

    def set_file_path(self, abs_path: str):
        self.set_param_value("file_path", abs_path)
        self.defocus_to_canvas()

    # ---------- 执行 ----------
    def current_bottom_tab(self) -> str:
        for t in self.win.descendants(control_type="TabItem"):
            try:
                if t.is_selected:
                    return t.window_text()
            except Exception:
                continue
        return ""

    def select_bottom_tab(self, name: str):
        for t in self.win.descendants(control_type="TabItem"):
            if t.window_text() == name:
                real_click(*_mid(t.rectangle()))
                time.sleep(0.25)
                return

    def profile_summary_text(self) -> str:
        """Profile 页摘要文本（执行完成后自动成为当前页、必可见）。

        格式：'Average of N frames | X ms total | T tasks (A ok, B failed,
        C skipped) | Critical Path: ... | Parallel Efficiency: ...%'
        """
        for t in self.win.descendants(control_type="Text"):
            try:
                txt = t.window_text() or ""
            except Exception:
                continue
            if PROFILE_RE.search(txt):
                return txt
        return ""

    def ensure_log_visible(self) -> bool:
        """确保 Log 页可见可读（执行完成会自动切到 Profile，隐藏 Log 使其
        UIA Name 变空）。先 ^Tab 循环（焦点不在标签栏时无效），再用真实
        点击 Log 标签兜底。"""
        for _ in range(2):
            if self.log_text():
                return True
            send_keys("^{TAB}")
            time.sleep(0.35)
        if self.log_text():
            return True
        try:
            self.select_bottom_tab("Log")
        except Exception:
            pass
        return bool(self.log_text())

    def wait_run_finished(self, prior_finished: int, timeout: float = 60.0,
                          base_summary: str | None = None) -> tuple[int, int, str]:
        """等待本轮执行完成并解析 ok/failed。

        主信号：Profile 摘要文本相对 base_summary 变化——onExecutionFinished
        无条件切到 Profile 页并刷新摘要（帧计数递增，文本必然变化）。
        base_summary 必须是 Run 点击**前**捕获的（小图毫秒级完成，点击后
        再取基准=取到完成态）。辅信号：日志新增 finished 行。
        """
        if base_summary is None:
            base_summary = self.profile_summary_text()

        def _done():
            cur = self.profile_summary_text()
            if os.environ.get("E2E_DEBUG"):
                n_texts = len(self.win.descendants(control_type="Text"))
                print(f"[E2E_DEBUG] poll: profile={bool(cur)} base={bool(base_summary)} "
                      f"cur==base={cur == base_summary} texts={n_texts}", flush=True)
            if cur and cur != base_summary:
                m = PROFILE_RE.search(cur)
                if m:
                    return (int(m.group(1)), int(m.group(2)), cur)
            self.ensure_log_visible()
            ms = FINISHED_RE.findall(self.log_text())
            if len(ms) > prior_finished:
                ok, failed = ms[prior_finished]
                return (int(ok), int(failed), self.log_text())
            return None

        return wait_until(_done, timeout=timeout, interval=0.6,
                          desc="执行完成（Profile 摘要更新）")

    def run_and_wait(self, timeout: float = 25.0) -> tuple[int, int, str]:
        """点击 Run 并等待完成。

        基准摘要必须在点击**前**捕获——小图执行 ~2ms，点击后再读摘要已是
        完成态，变化检测永远不触发（帧计数每次 +1，摘要文本必然变化）。
        首击被干扰偷走时：小图 25s 无摘要变化即可判死，重击一次再等 20s
        （此前 60s+30s 的宽松超时让被偷的用例白等近两分钟才自愈）。
        """
        prior = self.finished_counts()
        base_summary = self.profile_summary_text()
        for attempt in range(2):
            self.click_run()
            try:
                return self.wait_run_finished(
                    prior, timeout=(20.0 if attempt else timeout),
                    base_summary=base_summary)
            except AppError:
                if attempt:
                    raise
                time.sleep(0.5)
        raise AppError("unreachable")

    # ---------- 原生文件对话框 ----------
    def _native_dialog(self):
        """按 Win32 归属找模态文件对话框（class #32770 且 GW_OWNER == 主窗口）。

        MSIX 激活链路会把对话框放在与 UIA 主窗口不同的宿主进程里，
        按 pid 过滤找不到；owner 链是 Win32 语义上的正确归属。
        """
        main = self.win.handle
        for hwnd in _enum_windows():
            try:
                if not _user32.IsWindowVisible(hwnd):
                    continue
                if _user32.GetWindow(hwnd, GW_OWNER) != main:
                    continue
                if _win_class(hwnd) == "#32770":
                    return hwnd
            except Exception:
                continue
        return None

    def _filename_edit(self, dlg):
        """定位文件名输入框。

        对话框里有多个 Edit（文件列表项、搜索框、文件名框），不能按树序取
        最后一个——实测那是顶部搜索框，路径打进搜索框后 ENTER 变成执行搜索、
        打开按钮因文件名为空而无动作。判定：Name 以「文件名/File name」开头
        （UIA 把行标签并进 Name）；退回位置启发（底部区域且不在右侧搜索区）。
        """
        dr = dlg.rectangle()
        edits = []
        for e in dlg.descendants(control_type="Edit"):
            try:
                edits.append((e, e.rectangle()))
            except Exception:
                continue
        name_markers = ("文件名", "File name", "File Name", "Filename")
        for e, _r in edits:
            try:
                if any((e.window_text() or "").startswith(m) for m in name_markers):
                    return e
            except Exception:
                continue
        # 位置回退：靠近底部且位于左半区（搜索框在顶部右侧）
        bottom_area = [(e, r) for e, r in edits
                       if r.bottom > dr.bottom - 150
                       and r.left < dr.left + (dr.right - dr.left) * 0.6]
        if bottom_area:
            return max(bottom_area, key=lambda er: er[1].bottom)[0]
        return edits[-1][0] if edits else None

    def _dialog_confirm(self, hwnd, dlg):
        """确认文件对话框（打开/保存）。

        实测结论：IFileDialog 是 DirectUI 实现——EnumChildWindows 枚举到的
        Button 类只是壳 HWND，BM_CLICK 天生无效；本机还存在持续重设光标的
        外部干扰使真实点击不可靠。因此主通道用 **UIA Invoke**（DirectUI
        程序化激活的标准通道，与光标/输入队列/Win32 消息全部无关），
        后备：BM_CLICK → 位置启发点击 → ENTER。
        """
        WM_COMMAND = 0x0111
        BM_CLICK = 0x00F5
        confirm_texts = ("打开", "Open", "保存", "Save")

        # 主通道：UIA Invoke
        for _attempt in range(3):
            try:
                for b in dlg.descendants(control_type="Button"):
                    t = (b.window_text() or "").strip()
                    if any(t == x or t.startswith(x + "(") or t.startswith(x + " (")
                           for x in confirm_texts):
                        b.invoke()
                        time.sleep(0.4)
                        if not _user32.IsWindowVisible(hwnd):
                            return
            except Exception:
                time.sleep(0.3)
                continue

        # 后备 1：BM_CLICK（对经典 #32770 有效）
        buttons: list[tuple[int, str]] = []

        @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
        def on_child(ch, _lp):
            if _win_class(ch) == "Button":
                buttons.append((ch, _win_title(ch)))
            return True

        _user32.EnumChildWindows(hwnd, on_child, 0)
        for ch, title in buttons:
            t = title.strip()
            if any(t == x or t.startswith(x + "(") or t.startswith(x + " (")
                   for x in confirm_texts):
                _user32.PostMessageW(ch, BM_CLICK, 0, 0)
                time.sleep(0.4)
                if not _user32.IsWindowVisible(hwnd):
                    return
        _user32.PostMessageW(hwnd, WM_COMMAND, 1, 0)
        time.sleep(0.3)
        if not _user32.IsWindowVisible(hwnd):
            return

        # 后备 2：位置启发（底部按钮行 x 最小 = OK）
        try:
            dr = dlg.rectangle()
            bottom = [b for b in dlg.descendants(control_type="Button")
                      if b.rectangle().bottom > dr.bottom - 90
                      and b.rectangle().right < dr.right - 8]
            if bottom:
                target = min(bottom, key=lambda b: b.rectangle().left)
                real_click(*_mid(target.rectangle()))
                time.sleep(0.4)
                if not _user32.IsWindowVisible(hwnd):
                    return
        except Exception:
            pass
        send_keys("{ENTER}")
        time.sleep(0.3)
        if _user32.IsWindowVisible(hwnd):
            send_keys("{ENTER}")
            time.sleep(0.3)

    def dialog_type_path(self, path, timeout: float = 15.0):
        """在系统文件对话框输入完整路径并确认（locale 无关）。

        输入用直接键入（转义后 send_keys，真实键盘事件；剪贴板粘贴在干扰
        环境下偶发丢失）。输入校验失败重试一次，仍失败回退 UIA 值写入；
        确认优先点击「打开/保存」按钮（ENTER 键在焦点被抢时落空）。
        """
        path = str(path)
        hwnd = wait_until(self._native_dialog, timeout=timeout, desc="文件对话框出现")
        dlg = Desktop(backend="uia").window(handle=hwnd)
        edit = self._filename_edit(dlg)
        if edit is None:
            self.report.screenshot(dlg.capture_as_image(), "dialog_no_edit.png")
            raise AppError("文件对话框内未找到文件名编辑框")

        def _attempt_typing() -> bool:
            real_click(*_mid(edit.rectangle()))
            send_keys("^a")
            time.sleep(0.1)
            send_keys(escape_keys(path))
            time.sleep(0.25)
            try:
                typed = edit.window_text() or ""
            except Exception:
                typed = ""
            return path in typed or path.split("\\")[-1] in typed

        if not _attempt_typing():
            if not _attempt_typing():
                # 最后回退：UIA Value 写入（非真实键入，但对话框交互仍是真实 UI）
                edit.set_edit_text(path)
                time.sleep(0.2)
        self._dialog_confirm(hwnd, dlg)
        wait_until(lambda: not _user32.IsWindowVisible(hwnd), timeout=10,
                   desc="文件对话框关闭")

    def menu_open_file(self, path, attempts: int = 2):
        """File>Open + 对话框输入路径。整段重试：点击可能被光标干扰抢走，
        菜单操作成功但对话框没弹也按失败处理重来。"""
        for attempt in range(attempts):
            try:
                self.menu_click("File", "Open...")
                self.dialog_type_path(path)
                return
            except AppError:
                send_keys("{ESC}")
                time.sleep(0.3)
                if attempt == attempts - 1:
                    raise

    def menu_save_as(self, path, attempts: int = 2):
        for attempt in range(attempts):
            try:
                self.menu_click("File", "Save As...")
                self.dialog_type_path(path)
                return
            except AppError:
                send_keys("{ESC}")
                time.sleep(0.3)
                if attempt == attempts - 1:
                    raise

    # ---------- 合成文件拖拽 ----------
    def drop_file_via_explorer(self, path, timeout: float = 20.0):
        """真实用户拖放：打开 Explorer 选中文件，真实鼠标从 Explorer 拖到画布。

        Qt6 窗口注册为 OLE drop target，会忽略 WM_DROPFILES——必须走真实
        OLE 拖放（Explorer 作为拖放源），这也正是用户的实际操作方式。
        """
        path = Path(path)
        own = getattr(self, "_own_explorers", None)
        if own is None:
            own = self._own_explorers = set()
        # 外部窗口 = 不是本 harness 此前打开且仍存活的（重试时旧窗口不算外部）
        external = {w.handle for w in self._explorer_windows()} - own
        subprocess.Popen(["explorer.exe", "/select,", str(path)])
        folder = path.parent.name
        # /select 可能复用已有 Explorer 进程并新开窗口；只认标题含目标目录的
        # 新窗口（残留旧窗口指向别的目录，在里面找文件项必然失败）
        def _new_win():
            for w in self._explorer_windows():
                if w.handle in external or w.handle in own:
                    continue
                if folder in (w.window_text() or ""):
                    return w
            return None

        exp = wait_until(_new_win, timeout=timeout,
                         desc=f"Explorer 窗口（{folder}）出现")
        own.add(exp.handle)
        # 把 Explorer 挪到应用窗口右侧：默认开窗位置可能盖住画布，
        # 拖拽落点被它挡住 = 拖回 Explorer 自己（实测失败模式之一）
        try:
            screen_w = _user32.GetSystemMetrics(0)
            app_r = self.win.rectangle()
            ex = min(max(app_r.right + 20, 40), max(40, screen_w - 1000))
            _user32.SetWindowPos(exp.handle, 0, ex, 30, 980, 900, 0x0004)
            time.sleep(0.4)
        except Exception:
            pass
        time.sleep(1.2)   # 视图加载
        stem = path.stem
        wanted = {path.name, stem}   # Explorer 默认隐藏已知扩展名，只显示 stem
        try:
            item = wait_until(
                lambda: next((it for it in exp.descendants(control_type="ListItem")
                              if (it.window_text() or "").split("\n")[0] in wanted), None),
                timeout=timeout, desc=f"Explorer 列表项 {path.name}")
        except AppError:
            try:
                self.report.screenshot(exp.capture_as_image(), "explorer_no_item.png")
            except Exception:
                pass
            raise
        target = self.canvas_center()
        real_press(*_mid(item.rectangle()))
        real_move(target[0], target[1], steps=18, delay=0.05)  # 慢拖给 OLE 协商留时间
        time.sleep(0.25)
        real_release(target[0], target[1])
        time.sleep(0.6)
        for w in self._explorer_windows():   # 清理本次及历次重试新开的窗口
            if w.handle not in external:
                try:
                    w.close()
                    own.discard(w.handle)
                except Exception:
                    pass

    def _explorer_windows(self) -> list:
        wins = []
        for w in Desktop(backend="uia").windows():
            try:
                if w.class_name() in ("CabinetWClass", "ExploreWClass"):
                    wins.append(w)
            except Exception:
                continue
        return wins

    def drop_file(self, path, screen_pt: tuple[int, int] | None = None):
        """向主窗口投递 WM_DROPFILES —— Qt 转成 QDropEvent，走真实 dropEvent 路径。"""
        pt = screen_pt or self.canvas_center()
        wr = self.win.rectangle()
        client = (pt[0] - wr.left, pt[1] - wr.top)
        payload = (struct.pack("Illii", 20, client[0], client[1], 0, 1)
                   + str(path).encode("utf-16-le") + b"\x00\x00")
        hglob = _kernel32.GlobalAlloc(GMEM_MOVEABLE, len(payload))
        if not hglob:
            raise AppError("GlobalAlloc 失败")
        ptr = _kernel32.GlobalLock(hglob)
        if not ptr:
            raise AppError("GlobalLock 失败")
        ctypes.memmove(ptr, payload, len(payload))
        _kernel32.GlobalUnlock(hglob)
        # 接收方（Qt dragDrop）DragFinish 负责释放 hglob
        _user32.SendMessageW(ctypes.c_void_p(self.win.handle), WM_DROPFILES,
                             hglob, None)
        time.sleep(0.5)
