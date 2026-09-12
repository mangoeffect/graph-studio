# -*- coding: utf-8 -*-
"""ax_driver.py — macOS AXAPI + CGEvent E2E 驱动（ctypes，零第三方依赖）。

与 pyobjc/atomacos 同走系统 AXAPI；自制薄封装原因见包 __init__。
能力：
  - AX 控件树读取（role/title/description/value/position/size）与 AXPress
  - 菜单驱动（原生 NSMenu：menubar 与右键上下文菜单）
  - CGEvent 鼠标（点击/拖拽/右键）与键盘（组合键/Unicode 文本）

权限（TCC）：本进程所属终端/IDE 需要「辅助功能」授权，see dev-docs/e2e-macos.md。
"""

import ctypes
import time
from ctypes import (POINTER, Structure, byref, c_bool, c_char_p, c_double,
                    c_int, c_long, c_uint, c_ulong, c_ushort, c_void_p)

_cf = ctypes.cdll.LoadLibrary(
    "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation")
_as = ctypes.cdll.LoadLibrary(
    "/System/Library/Frameworks/ApplicationServices.framework/ApplicationServices")
_cg = ctypes.cdll.LoadLibrary(
    "/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics")


class CGPoint(Structure):
    _fields_ = [("x", c_double), ("y", c_double)]


class CGSize(Structure):
    _fields_ = [("w", c_double), ("h", c_double)]


# ---- 常量 ----
kCFStringEncodingUTF8 = 0x08000100
kCGHIDEventTap = 0
kCGEventLeftMouseDown, kCGEventLeftMouseUp = 1, 2
kCGEventRightMouseDown, kCGEventRightMouseUp = 3, 4
kCGEventMouseMoved = 5
kCGMouseButtonLeft, kCGMouseButtonRight = 0, 1
kCGEventFlagMaskCommand = 1 << 20
kCGEventFlagMaskShift = 1 << 17
kAXValueCGPointType, kAXValueCGSizeType = 1, 2
# 常用虚拟键码（HIToolbox Events.h）
kVK_Return, kVK_Delete, kVK_ANSI_G = 0x24, 0x33, 0x05
kVK_ANSI_R, kVK_ANSI_Q, kVK_ANSI_Z = 0x0F, 0x0C, 0x06

# AX 属性/动作名
A = {
    "children": "AXChildren",
    "role": "AXRole",
    "title": "AXTitle",
    "desc": "AXDescription",
    "value": "AXValue",
    "position": "AXPosition",
    "size": "AXSize",
    "menu": "AXMenu",
    "menubar": "AXMenuBar",
    "windows": "AXWindows",
    "focused": "AXFocusedWindow",
    "press": "AXPress",
}


class AxDriverError(RuntimeError):
    pass


# ---- CF 原型 ----
_cf.CFStringCreateWithCString.restype = c_void_p
_cf.CFStringCreateWithCString.argtypes = [c_void_p, c_char_p, c_uint]
_cf.CFStringGetCString.restype = c_bool
_cf.CFStringGetCString.argtypes = [c_void_p, c_char_p, c_long, c_uint]
_cf.CFArrayGetCount.restype = c_long
_cf.CFArrayGetCount.argtypes = [c_void_p]
_cf.CFArrayGetValueAtIndex.restype = c_void_p
_cf.CFArrayGetValueAtIndex.argtypes = [c_void_p, c_long]
_cf.CFRelease.argtypes = [c_void_p]
_cf.CFGetTypeID.restype = c_ulong
_cf.CFGetTypeID.argtypes = [c_void_p]
_cf.CFArrayGetTypeID.restype = c_ulong
_cf.CFArrayGetTypeID.argtypes = []

# ---- AX 原型 ----
_as.AXIsProcessTrusted.restype = c_bool
_as.AXIsProcessTrusted.argtypes = []
_as.AXUIElementCreateApplication.restype = c_void_p
_as.AXUIElementCreateApplication.argtypes = [c_int]
_as.AXUIElementCopyAttributeValue.restype = c_int
_as.AXUIElementCopyAttributeValue.argtypes = [c_void_p, c_void_p,
                                              POINTER(c_void_p)]
_as.AXUIElementPerformAction.restype = c_int
_as.AXUIElementPerformAction.argtypes = [c_void_p, c_void_p]
_as.AXUIElementSetAttributeValue.restype = c_int
_as.AXUIElementSetAttributeValue.argtypes = [c_void_p, c_void_p, c_void_p]
_as.AXValueGetValue.restype = c_bool
_as.AXValueGetValue.argtypes = [c_void_p, c_uint, c_void_p]

# ---- CG 原型 ----
_cg.CGEventCreateMouseEvent.restype = c_void_p
_cg.CGEventCreateMouseEvent.argtypes = [c_void_p, c_uint, CGPoint, c_uint]
_cg.CGEventCreateKeyboardEvent.restype = c_void_p
_cg.CGEventCreateKeyboardEvent.argtypes = [c_void_p, c_ushort, c_bool]
_cg.CGEventSetFlags.argtypes = [c_void_p, c_ulong]
_cg.CGEventKeyboardSetUnicodeString.argtypes = [c_void_p, c_ulong, c_void_p]
_cg.CGEventPost.argtypes = [c_uint, c_void_p]
_cg.CGEventPostToPid.restype = c_int
_cg.CGEventPostToPid.argtypes = [c_int, c_void_p]
_cg.CGEventSetIntegerValueField.restype = None
_cg.CGEventSetIntegerValueField.argtypes = [c_void_p, c_uint, c_long]
_cg.CFRelease = _cf.CFRelease

kCGMouseEventClickState = 1   # CGEventField：第几次点击（双击须标 2）


def _cfstr(s: str) -> c_void_p:
    ref = _cf.CFStringCreateWithCString(None, s.encode("utf-8"),
                                        kCFStringEncodingUTF8)
    if not ref:
        raise AxDriverError(f"CFStringCreateWithCString 失败: {s!r}")
    return ref


def _cf_to_str(ref: c_void_p) -> str:
    if not ref:
        return ""
    buf = ctypes.create_string_buffer(4096)
    ok = _cf.CFStringGetCString(ref, buf, 4096, kCFStringEncodingUTF8)
    return buf.value.decode("utf-8", "replace") if ok else ""


class AxElement:
    """AXUIElement/AXUIItem 的 RAII 包装。"""

    def __init__(self, ref: c_void_p):
        if not ref:
            raise AxDriverError("AX 元素引用为空")
        self.ref = ref

    def __del__(self):
        try:
            _cf.CFRelease(self.ref)
        except (AttributeError, TypeError):
            pass

    def attr(self, name: str):
        out = c_void_p()
        err = _as.AXUIElementCopyAttributeValue(self.ref, _cfstr(name),
                                                byref(out))
        if err != 0 or not out:
            return None
        return out

    def attr_str(self, name: str) -> str:
        ref = self.attr(name)
        if not ref:
            return ""
        try:
            return _cf_to_str(ref)
        finally:
            _cf.CFRelease(ref)

    def children(self) -> list["AxElement"]:
        arr = self.attr(A["children"])
        if not arr:
            return []
        try:
            # 数组内是非拥有引用，retain 后交给 AxElement 持有
            return [AxElement(_retain(el)) for el in _iter_cf_array(arr)]
        finally:
            _cf.CFRelease(arr)

    def role(self) -> str:
        return self.attr_str(A["role"])

    def title(self) -> str:
        return self.attr_str(A["title"])

    def description(self) -> str:
        return self.attr_str(A["desc"])

    def value_text(self) -> str:
        return self.attr_str(A["value"])

    def set_value_text(self, text: str) -> bool:
        """AXValue 直写（文本框）。原生 AppKit 控件可靠支持；Qt 控件视
        可编辑性而定。用于 NSOpenPanel 前往文件夹 sheet 的路径填充。"""
        vref = _cfstr(text)
        try:
            return _as.AXUIElementSetAttributeValue(
                self.ref, _cfstr(A["value"]), vref) == 0
        finally:
            _cf.CFRelease(vref)

    def position(self) -> CGPoint:
        ref = self.attr(A["position"])
        if not ref:
            return CGPoint(-1, -1)
        pt = CGPoint()
        try:
            if not _as.AXValueGetValue(ref, kAXValueCGPointType, byref(pt)):
                return CGPoint(-1, -1)
        finally:
            _cf.CFRelease(ref)
        return pt

    def size(self) -> CGSize:
        ref = self.attr(A["size"])
        if not ref:
            return CGSize(0, 0)
        sz = CGSize()
        try:
            if not _as.AXValueGetValue(ref, kAXValueCGSizeType, byref(sz)):
                return CGSize(0, 0)
        finally:
            _cf.CFRelease(ref)
        return sz

    def press(self) -> bool:
        return self.perform(A["press"])

    def perform(self, *actions: str) -> bool:
        """按顺序尝试动作名，任一成功即真（AXMenuBarItem 只支持
        AXShowMenu 而非 AXPress 等）。"""
        for action in actions:
            if _as.AXUIElementPerformAction(self.ref, _cfstr(action)) == 0:
                return True
        return False

    def walk(self):
        """先序遍历整棵子树（含自身）。"""
        yield self
        for child in self.children():
            yield from child.walk()


_cf.CFRetain.restype = None
_cf.CFRetain.argtypes = [c_void_p]


def _retain(el: c_void_p) -> c_void_p:
    """CFArrayGetValueAtIndex 拿到的是非拥有引用，CFRetain 后交给 AxElement。"""
    _cf.CFRetain(el)
    return el


def _iter_cf_array(ref) -> list[c_void_p]:
    """把 CFArrayRef 展开为元素列表；非数组 CFType（对它 CFArrayGetCount 会
    NSException 直接杀进程——AXChildren 某些元素返回 AXValue/单元素）返回空。"""
    if not ref or _cf.CFGetTypeID(ref) != _cf.CFArrayGetTypeID():
        return []
    return [v for i in range(_cf.CFArrayGetCount(ref))
            if (v := _cf.CFArrayGetValueAtIndex(ref, i))]


def ensure_accessibility(prompt: bool = True) -> bool:
    """探测辅助功能权限。prompt 仅影响未授权时的文案行为差异（系统弹窗
    引导由入口脚本承担，这里始终只做无副作用探测）。"""
    return bool(_as.AXIsProcessTrusted())


class MacDriver:
    """面向单目标 app 的驱动：attach + 菜单 + 输入注入 + AX 查询。"""

    def __init__(self, pid: int):
        self.pid = pid
        ref = _as.AXUIElementCreateApplication(pid)
        if not ref:
            raise AxDriverError(f"AXUIElementCreateApplication({pid}) 失败")
        self.app = AxElement(ref)

    # ---- AX 查询 ----

    def windows(self) -> list[AxElement]:
        arr = self.app.attr(A["windows"])
        if not arr:
            return []
        try:
            return [AxElement(_retain(el)) for el in _iter_cf_array(arr)]
        finally:
            _cf.CFRelease(arr)

    def focus_window(self, window: AxElement) -> bool:
        """把窗口带到前台：AXRaise + 设为 app 的 AXFocusedWindow。

        NSOpenPanel 出现后焦点未必立刻移交（主窗口仍持有键盘），不聚焦
        则 Cmd+Shift+G 等快捷键打到主窗口上。"""
        window.perform("AXRaise")
        return _as.AXUIElementSetAttributeValue(
            self.app.ref, _cfstr("AXFocusedWindow"), window.ref) == 0

    def window_by_title(self, substr: str, timeout: float = 30.0) -> AxElement:
        deadline = time.time() + timeout
        while time.time() < deadline:
            for w in self.windows():
                if substr in w.title():
                    return w
            time.sleep(0.4)
        raise AxDriverError(f"{timeout}s 内未找到标题含 {substr!r} 的窗口")

    def find(self, window: AxElement, pred, timeout: float = 10.0):
        """在窗口子树里按谓词找元素。pred(AxElement) -> bool。"""
        deadline = time.time() + timeout
        while time.time() < deadline:
            for el in window.walk():
                try:
                    if pred(el):
                        return el
                except (AxDriverError, OSError):
                    continue
            time.sleep(0.3)
        return None

    def find_by_description(self, window: AxElement, name: str,
                            timeout: float = 10.0):
        """按 Qt accessibleName 找元素。实测（dump_ax_tree）Qt 把 accessibleName
        映射到 **AXTitle**（Log Panel/Task Library/Image Results 均如此），
        个别角色（按钮等）落在 AXDescription——两者都匹配。"""
        return self.find(window,
                         lambda el: el.title() == name or el.description() == name,
                         timeout)

    def find_static_text_starting(self, window: AxElement, prefix: str,
                                  timeout: float = 10.0):
        return self.find(window, lambda el: el.role() == "AXStaticText"
                         and el.value_text().startswith(prefix), timeout)

    def menu_click(self, *titles: str, timeout: float = 15.0) -> bool:
        """点击主菜单路径（如 menu_click("File", "Open...")）。

        原生 menubar（QMenuBar 在 macOS 映射 NSMenu）的项上 AXPress 返回
        成功但**不触发 Qt 动作**（实测 File>Open 面板不弹）——一律用真实
        鼠标点击项中心。menubar 顶层项的菜单挂在其 AXChildren（无 AXMenu
        属性）。
        """
        bar_ref = self.app.attr(A["menubar"])
        if not bar_ref:
            raise AxDriverError("无 AXMenuBar")
        bar = AxElement(bar_ref)
        top = None
        for wrapped in bar.children():
            if wrapped.title() == titles[0]:
                top = wrapped
                break
        if top is None:
            return False

        if len(titles) == 1:
            return self._click_menu_item(top)

        current = top
        for title in titles[1:]:
            # 真实点击展开 current，在其菜单里找下一级（叶子在循环后点击）
            if not self._click_menu_item(current):
                return False
            menu = self._menu_of(current)
            if menu is None:
                return False
            nxt = self._find_in_menu(menu, title, timeout)
            if nxt is None:
                return False
            current = nxt
        return self._click_menu_item(current)

    def _find_in_menu(self, menu: AxElement, title: str, timeout: float):
        deadline = time.time() + timeout
        while time.time() < deadline:
            for cand in menu.children():
                if cand.role() == "AXMenuItem" and cand.title().startswith(title):
                    return cand
            time.sleep(0.2)
        return None

    def _click_menu_item(self, item: AxElement) -> bool:
        """真实鼠标点击菜单项中心（几何缺失时回退 AXPress）。"""
        pos, size = item.position(), item.size()
        if pos.x < 0 or size.w <= 0:
            return item.perform(A["press"], "AXShowMenu")
        self.click(pos.x + size.w / 2, pos.y + size.h / 2)
        time.sleep(0.25)
        return True

    @staticmethod
    def _menu_of(item: AxElement, timeout: float = 5.0):
        """菜单项的下拉菜单：优先 AXMenu 属性；原生 menubar 顶层项没有
        AXMenu 属性，菜单挂在其 AXChildren（role=AXMenu）里。"""
        deadline = time.time() + timeout
        while time.time() < deadline:
            ref = item.attr(A["menu"])
            if ref:
                return AxElement(ref)
            for child in item.children():
                if child.role() == "AXMenu":
                    return child
            time.sleep(0.2)
        return None

    def press_context_menu(self, *path: str, timeout: float = 15.0) -> str:
        """在已右键弹出的上下文菜单里按路径点选项（叶子触发动作）。

        画布菜单是两级：分类子菜单（如 "Input"）-> task 类型叶子。子菜单
        QMenu 悬停展开后才进 AX 树——鼠标移到分类项上等子菜单窗口出现，
        再在叶子项上 AXPress。
        """
        current = self._find_menu_item(path[0], timeout)
        if current is None:
            raise AxDriverError(f"上下文菜单未出现 {path[0]!r}")
        for title in path[1:]:
            nxt = self._hover_open_submenu(current, title)
            if nxt is None:
                raise AxDriverError(f"子菜单项 {title!r} 未出现")
            current = nxt
        if not current.press():
            raise AxDriverError(f"菜单项 {path[-1]!r} AXPress 失败")
        # AXPress 触发动作但不一定收起 Qt 弹出菜单（无真实鼠标事件参与）
        # ——QMenu::exec 是模态 grab，残留的父级菜单会吞掉后续输入。ESC
        # 逐个收，直到没有菜单窗口残留。
        for _ in range(4):
            if not self._popup_menu_windows():
                break
            self.key_combo(0x35)
            time.sleep(0.25)
        return current.title()

    def _popup_menu_windows(self) -> bool:
        """是否有 Qt 弹出菜单窗口残留（含 AXMenuItem 的非主窗口；原生
        menubar 的下拉不挂在 windows() 里，不受影响）。"""
        for w in self.windows():
            try:
                if w.title():
                    continue
                for el in w.walk():
                    if el.role() == "AXMenuItem":
                        return True
            except (AxDriverError, OSError):
                continue
        return False

    def _find_menu_item(self, title: str, timeout: float):
        deadline = time.time() + timeout
        while time.time() < deadline:
            for w in self.windows():
                for el in w.walk():
                    if (el.role() == "AXMenuItem"
                            and el.title().startswith(title)):
                        return el
            # 上下文菜单也可能挂在 app 直接子节点
            for el in self.app.walk():
                if el.role() == "AXMenuItem" and el.title().startswith(title):
                    return el
            time.sleep(0.3)
        return None

    def _hover_open_submenu(self, item: AxElement, title: str,
                            timeout: float = 5.0):
        """鼠标悬停菜单项展开子菜单，返回其中标题前缀匹配的项。

        Qt 弹出菜单（QMenu::exec）以 **AXWindow** 形态出现（不是 AXMenu），
        悬停展开的子菜单同理——按「任意窗口里的 AXMenuItem」扫描。"""
        pos, size = item.position(), item.size()
        self.mouse_move(pos.x + size.w * 0.85, pos.y + size.h / 2)
        deadline = time.time() + timeout
        while time.time() < deadline:
            for w in self.windows():
                for el in w.walk():
                    if (el.role() == "AXMenuItem"
                            and el.title().startswith(title)):
                        return el
            time.sleep(0.15)
        return None

    # ---- CGEvent 输入 ----

    def _post(self, ev):
        _cg.CGEventPost(kCGHIDEventTap, ev)
        _cf.CFRelease(ev)

    def click(self, x: float, y: float, right: bool = False,
              double: bool = False):
        down = kCGEventRightMouseDown if right else kCGEventLeftMouseDown
        up = kCGEventRightMouseUp if right else kCGEventLeftMouseUp
        btn = kCGMouseButtonRight if right else kCGMouseButtonLeft
        self.mouse_move(x, y)
        time.sleep(0.05)
        for click_no in (1, 2) if double else ((1,)):
            # 双击必须带 clickState 标记（AppKit 不按时间窗推断合成事件，
            # 无标记时第二击仍算单击——只选中不确认）
            state = click_no if double else 1

            def _mk(kind, s):
                ev = _cg.CGEventCreateMouseEvent(None, kind, CGPoint(x, y), btn)
                _cg.CGEventSetIntegerValueField(ev, kCGMouseEventClickState, s)
                return ev

            self._post(_mk(down, state))
            self._post(_mk(up, state))
            if double and click_no == 1:
                time.sleep(0.12)

    def mouse_move(self, x: float, y: float):
        """悬停移动（不点击）——展开子菜单用。"""
        self._post(_cg.CGEventCreateMouseEvent(
            None, kCGEventMouseMoved, CGPoint(x, y), kCGMouseButtonLeft))

    def drag(self, x0, y0, x1, y1, steps=12, right=False):
        btn = kCGMouseButtonRight if right else kCGMouseButtonLeft
        down = kCGEventRightMouseDown if right else kCGEventLeftMouseDown
        up = kCGEventRightMouseUp if right else kCGEventLeftMouseUp
        self._post(_cg.CGEventCreateMouseEvent(None, kCGEventMouseMoved,
                                               CGPoint(x0, y0), btn))
        self._post(_cg.CGEventCreateMouseEvent(None, down, CGPoint(x0, y0),
                                               btn))
        for i in range(1, steps + 1):
            self._post(_cg.CGEventCreateMouseEvent(
                None, kCGEventMouseMoved,
                CGPoint(x0 + (x1 - x0) * i / steps,
                        y0 + (y1 - y0) * i / steps), btn))
            time.sleep(0.02)
        self._post(_cg.CGEventCreateMouseEvent(None, up, CGPoint(x1, y1), btn))

    def _key(self, vkey: int, flags: int = 0, down: bool = True,
             text: str = ""):
        ev = _cg.CGEventCreateKeyboardEvent(None, vkey, down)
        if flags:
            _cg.CGEventSetFlags(ev, flags)
        if text and down:
            u = text.encode("utf-16-le")
            # 缓冲固定 64 字节（32 个 UTF-16 码元）：截断后补零，
            # from_buffer_copy 不自己补齐；长度按码元数取 min。
            n = min(len(u) // 2, 32)
            buf = (c_ushort * 32).from_buffer_copy(u[:64].ljust(64, b"\x00"))
            _cg.CGEventKeyboardSetUnicodeString(ev, n, buf)
        # 键盘定向投递到目标进程：全局 HID tap 受系统焦点竞态影响
        # （NSOpenPanel 的前往 sheet 实测字符丢失），按 pid 投递由 AppKit
        # 交给该进程当前聚焦的控件。
        _cg.CGEventPostToPid(self.pid, ev)
        _cf.CFRelease(ev)

    def key_combo(self, vkey: int, flags: int = 0):
        self._key(vkey, flags, True)
        time.sleep(0.04)
        self._key(vkey, flags, False)

    def type_text(self, text: str):
        for ch in text:
            self._key(0, 0, True, text=ch)
            time.sleep(0.015)
            self._key(0, 0, False)
            time.sleep(0.015)
