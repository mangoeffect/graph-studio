# -*- coding: utf-8 -*-
"""dump_ax_tree.py — 采集 GraphStudio 主窗口的 AX 树（调试锚点用）。

在有辅助功能权限的终端里运行（与 run_e2e_macos.py 相同的 TCC 要求）：
  python3 scripts/e2e_macos/dump_ax_tree.py

动作：启动 dev .app -> 附着主窗口 -> 落盘整棵 AX 控件树（role/title/
description/identifier/help/value 前缀/位置尺寸）到 /tmp/gs_ax_dump.txt；
再验证 menubar 顶层菜单与 menu_click("File","New")；最后杀掉 app。
只读 + New（空画布上无副作用），不执行任何破坏性输入。
"""

import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from gs import console, repo_root  # noqa: E402
from e2e_macos.ax_driver import (AxDriverError, MacDriver,  # noqa: E402
                                 ensure_accessibility)

OUT = Path("/tmp/gs_ax_dump.txt")
KNOWN_NAMES = ["Graph Canvas", "Log Panel", "Task Library", "Output Panel",
               "Image Results"]

# 额外探测的属性（超出 ax_driver.A 的常规集合）
EXTRA_ATTRS = ["AXIdentifier", "AXHelp", "AXRoleDescription"]


def trunc(s: str, n: int = 60) -> str:
    s = s.replace("\n", "\\n")
    return s if len(s) <= n else s[:n - 3] + "..."


def dump_element(el, depth: int, lines: list, hits: dict):
    try:
        role = el.role()
    except Exception:
        role = "?"
    try:
        title = el.title()
    except Exception:
        title = ""
    try:
        desc = el.description()
    except Exception:
        desc = ""
    extras = {}
    for attr in EXTRA_ATTRS:
        try:
            extras[attr] = el.attr_str(attr)
        except Exception:
            extras[attr] = ""
    try:
        value = el.value_text()
    except Exception:
        value = ""
    try:
        pos, size = el.position(), el.size()
        geom = f" @({pos.x:.0f},{pos.y:.0f}) {size.w:.0f}x{size.h:.0f}"
    except Exception:
        geom = ""
    line = ("  " * depth + f"[{role}] title={trunc(title)!r}"
            f" desc={trunc(desc)!r} id={trunc(extras['AXIdentifier'])!r}"
            f" help={trunc(extras['AXHelp'])!r}"
            f" roleDesc={trunc(extras['AXRoleDescription'])!r}"
            f" value={trunc(value)!r}{geom}")
    lines.append(line)

    for name in KNOWN_NAMES:
        if name in (title, desc, extras["AXIdentifier"], extras["AXHelp"],
                    extras["AXRoleDescription"]):
            hits.setdefault(name, []).append(
                f"depth={depth} role={role} title={title!r} desc={desc!r} "
                f"id={extras['AXIdentifier']!r} help={extras['AXHelp']!r} "
                f"roleDesc={extras['AXRoleDescription']!r}")
    if role == "AXStaticText" and value.startswith("Nodes:"):
        hits.setdefault("countsLabel", []).append(f"value={value!r}")

    for child in el.children():
        dump_element(child, depth + 1, lines, hits)


def main() -> int:
    console.init()
    if not ensure_accessibility(prompt=True):
        console.fail("本终端没有辅助功能权限（与 run_e2e_macos.py 相同要求）")
        return 2

    # 复用入口的启动环境（插件/模型/DYLD 路径）
    from run_e2e_macos import default_app, launch_env
    root = repo_root()
    binary = default_app(root) / "Contents" / "MacOS" / "graph_studio"
    if not binary.is_file():
        console.fail(f"未找到 {binary}")
        return 1

    console.step(f"启动 {binary}")
    pid = os.posix_spawn(str(binary), [str(binary)], launch_env(root, "Release"))
    lines: list[str] = []
    hits: dict[str, list[str]] = {}
    try:
        drv = MacDriver(pid)
        window = drv.window_by_title("Graph Studio", timeout=45)
        console.ok(f"已附着（pid={pid}），开始 dump…")
        dump_element(window, 0, lines, hits)

        lines.append("")
        lines.append("==== menubar 顶层 ====")
        bar_ref = drv.app.attr("AXMenuBar")
        if bar_ref:
            from e2e_macos.ax_driver import AxElement
            for m in AxElement(bar_ref).children():
                try:
                    lines.append(f"  menu: {m.title()!r} (role={m.role()})")
                except Exception:
                    pass

        lines.append("")
        lines.append("==== File 菜单动作自省 ====")
        try:
            from e2e_macos.ax_driver import (AxElement, _cf, _cf_to_str,
                                             _iter_cf_array)
            bar_ref = drv.app.attr("AXMenuBar")
            file_item = None
            if bar_ref:
                file_item = next((m for m in AxElement(bar_ref).children()
                                  if m.title() == "File"), None)
            if file_item is not None:
                arr = file_item.attr("AXActions")
                if arr:
                    names = [_cf_to_str(r) for r in _iter_cf_array(arr)]
                    _cf.CFRelease(arr)
                    lines.append(f"  AXActions: {names}")
                for action in ("AXPress", "AXShowMenu", "AXOpen"):
                    ok = file_item.perform(action)
                    time.sleep(0.5)
                    menu_ref = file_item.attr("AXMenu")
                    ch = [c.title() for c in file_item.children()][:10]
                    lines.append(f"  perform({action}) -> {ok}; "
                                 f"AXMenu={'有' if menu_ref else '无'}; "
                                 f"children={ch}")
                    if menu_ref:
                        _cf.CFRelease(menu_ref)
                    drv.key_combo(0x35)   # ESC 收起
                    time.sleep(0.4)
            else:
                lines.append("  未找到 File 顶层项")
        except Exception as e:
            lines.append(f"  EXCEPTION: {type(e).__name__}: {e}")

        lines.append("")
        lines.append("==== menu_click('File', 'New') ====")
        try:
            ok = drv.menu_click("File", "New")
            lines.append(f"  result: {ok}")
            time.sleep(1.0)
            for w in drv.windows():
                try:
                    lines.append(f"  window: role={w.role()} title={w.title()!r}")
                except Exception:
                    pass
        except Exception as e:
            lines.append(f"  EXCEPTION: {type(e).__name__}: {e}")

        lines.append("")
        lines.append("==== Graph Canvas 锚点 ====")
        canvas = drv.find_by_description(window, "Graph Canvas", 5)
        if canvas is not None:
            pos, size = canvas.position(), canvas.size()
            lines.append(f"  found: @({pos.x:.0f},{pos.y:.0f}) {size.w:.0f}x{size.h:.0f}")
        else:
            lines.append("  未找到")

        if canvas is not None:
            lines.append("")
            lines.append("==== 右键菜单探查（含子菜单悬停） ====")
            try:
                pos, size = canvas.position(), canvas.size()
                drv.click(pos.x + size.w / 2, pos.y + size.h / 2, right=True)
                time.sleep(0.8)
                for w in drv.windows():
                    if w.role() in ("AXMenu", "AXWindow") and w.title() == "":
                        items = [(el.title(), el.position(), el.size())
                                 for el in w.walk() if el.role() == "AXMenuItem"]
                        if items:
                            lines.append(f"  popup role={w.role()}:")
                            for t, p, s in items[:14]:
                                lines.append(f"    {t!r} @({p.x:.0f},{p.y:.0f})"
                                             f" {s.w:.0f}x{s.h:.0f}")
                inp = drv._find_menu_item("Input", 5)
                if inp is None:
                    lines.append("  'Input' 项未找到")
                else:
                    p, s = inp.position(), inp.size()
                    lines.append(f"  Input @({p.x:.0f},{p.y:.0f}) {s.w:.0f}x{s.h:.0f}")
                    drv.mouse_move(p.x + s.w * 0.8, p.y + s.h / 2)
                    time.sleep(1.2)
                    for w in drv.windows():
                        items = [el.title() for el in w.walk()
                                 if el.role() == "AXMenuItem"]
                        if items:
                            lines.append(f"  hover后 {w.role()} 窗口: {items[:12]}")
                    drv.key_combo(0x35)   # ESC 收起
                    time.sleep(0.3)
            except Exception as e:
                lines.append(f"  EXCEPTION: {type(e).__name__}: {e}")
    finally:
        try:
            os.kill(pid, 15)
        except ProcessLookupError:
            pass

    lines.append("")
    lines.append("==== 已知 accessibleName 的落点 ====")
    for name in KNOWN_NAMES + ["countsLabel"]:
        found = hits.get(name)
        lines.append(f"  {name}: {found[0] if found else '未找到'}")

    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    console.ok(f"共 {len(lines)} 行 -> {OUT}")
    for name in KNOWN_NAMES + ["countsLabel"]:
        console.step(f"{name}: {'命中' if name in hits else '未找到'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
