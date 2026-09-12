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
_cg.CFRelease = _cf.CFRelease


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
        out = []
        try:
            for i in range(_cf.CFArrayGetCount(arr)):
                el = _cf.CFArrayGetValueAtIndex(arr, i)
                if el:
                    # 数组内是非拥有引用，retain 后交给 AxElement 持有
                    out.append(AxElement(_retain(el)))
        finally:
            _cf.CFRelease(arr)
        return out

    def role(self) -> str:
        return self.attr_str(A["role"])

    def title(self) -> str:
        return self.attr_str(A["title"])

    def description(self) -> str:
        return self.attr_str(A["desc"])

    def value_text(self) -> str:
        return self.attr_str(A["value"])

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
        return _as.AXUIElementPerformAction(self.ref, _cfstr(A["press"])) == 0

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
        out = []
        try:
            for i in range(_cf.CFArrayGetCount(arr)):
                el = _cf.CFArrayGetValueAtIndex(arr, i)
                if el:
                    out.append(AxElement(_retain(el)))
        finally:
            _cf.CFRelease(arr)
        return out

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
        return self.find(window, lambda el: el.description() == name, timeout)

    def find_static_text_starting(self, window: AxElement, prefix: str,
                                  timeout: float = 10.0):
        return self.find(window, lambda el: el.role() == "AXStaticText"
                         and el.value_text().startswith(prefix), timeout)

    def menu_click(self, *titles: str, timeout: float = 15.0) -> bool:
        """点击主菜单路径（如 menu_click("File", "Open...")）。

        走法：menubar 里按 title 找到顶层项 -> AXPress 展开 -> 读其 AXMenu
        -> 在 menu 项里按前缀找下一级 -> 逐级 AXPress（最后一级触发动作）。
        """
        bar = self.app.attr(A["menubar"])
        if not bar:
            raise AxDriverError("无 AXMenuBar")
        top = None
        try:
            for i in range(_cf.CFArrayGetCount(bar)):
                el = _cf.CFArrayGetValueAtIndex(bar, i)
                if el:
                    wrapped = AxElement(_retain(el))
                    if wrapped.title() == titles[0]:
                        top = wrapped
                        break
        finally:
            _cf.CFRelease(bar)
        if top is None:
            return False
        if len(titles) == 1:
            return top.press()

        current = top
        for depth, title in enumerate(titles[1:], start=1):
            if not current.press():
                return False
            menu_ref = None
            deadline = time.time() + 5
            while time.time() < deadline and menu_ref is None:
                menu_ref = current.attr(A["menu"])
                if menu_ref is None:
                    time.sleep(0.3)
            if menu_ref is None:
                return False
            menu = AxElement(menu_ref)
            found = None
            deadline = time.time() + timeout
            while time.time() < deadline and found is None:
                for cand in menu.children():
                    if cand.role() == "AXMenuItem" \
                            and cand.title().startswith(title):
                        found = cand
                        break
                time.sleep(0.3)
            if found is None:
                return False
            if depth == len(titles) - 1:
                return found.press()
            current = found
        return False

    def press_context_menu_item(self, substr: str, timeout: float = 15.0):
        """在已弹出的右键菜单里按标题前缀点选项（CGEvent 触发右键后调用）。"""
        deadline = time.time() + timeout
        while time.time() < deadline:
            for w in self.windows():
                if w.role() == "AXMenu":
                    for el in w.walk():
                        if (el.role() == "AXMenuItem"
                                and el.title().startswith(substr)):
                            if el.press():
                                return el.title()
            # 上下文菜单也可能挂在 app 直接子节点
            for el in self.app.walk():
                if el.role() == "AXMenuItem" and el.title().startswith(substr):
                    if el.press():
                        return el.title()
            time.sleep(0.3)
        raise AxDriverError(f"{timeout}s 内菜单未出现 {substr!r}")

    # ---- CGEvent 输入 ----

    def _post(self, ev):
        _cg.CGEventPost(kCGHIDEventTap, ev)
        _cf.CFRelease(ev)

    def click(self, x: float, y: float, right: bool = False,
              double: bool = False):
        down = kCGEventRightMouseDown if right else kCGEventLeftMouseDown
        up = kCGEventRightMouseUp if right else kCGEventLeftMouseUp
        btn = kCGMouseButtonRight if right else kCGMouseButtonLeft
        self._post(_cg.CGEventCreateMouseEvent(None, kCGEventMouseMoved,
                                               CGPoint(x, y), btn))
        time.sleep(0.05)
        for click_no in (1, 2) if double else ((1,)):
            self._post(_cg.CGEventCreateMouseEvent(None, down, CGPoint(x, y),
                                                   btn))
            self._post(_cg.CGEventCreateMouseEvent(None, up, CGPoint(x, y),
                                                   btn))
            if double and click_no == 1:
                time.sleep(0.12)

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
            buf = (c_ushort * 32).from_buffer_copy(u[:64])
            _cg.CGEventKeyboardSetUnicodeString(ev, len(u) // 2, buf)
        self._post(ev)

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
