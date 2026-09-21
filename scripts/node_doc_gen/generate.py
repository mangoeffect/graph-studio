#!/usr/bin/env python3
"""GraphStudio 节点手册页面生成器（官网 blog 侧）。

数据源（真值）：scripts/node_doc_gen/task_specs.json —— 由 probe_dump.cpp 从
真实构建产物导出（PluginRegistry 枚举 + 探针实例 param_specs/input_specs/
output_specs）。参数表以此为准，保证与代码一致。

文案：metadata_zh.py / metadata_en.py（每任务 title/summary/desc/notes +
任务级参数说明覆盖 COMMON_PARAMS 公共字典）。

输出：docs/content/{zh,en}/blog/<task_type>/index.md，slug = task type，
与 GraphStudio 属性面板 Docs 链接（<站点>/blog/<task_type>/）约定对齐。

用法（在仓库根裸文件名运行）：
    python3 scripts/node_doc_gen/generate.py            # 生成/覆盖 180 篇
    python3 scripts/node_doc_gen/generate.py --list     # 只列任务清单不写文件

设计文档：docs/research/node-doc-links-design.md
"""

import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))

from metadata_zh import COMMON_PARAMS as COMMON_ZH, META as META_ZH, MODULES as MODULES_ZH  # noqa: E402
from metadata_en import COMMON_PARAMS as COMMON_EN, META as META_EN, MODULES as MODULES_EN  # noqa: E402

# 节点手册文章的发表日期按模块错开：全部早于 2026-08 起的普通博文，
# 首页/blog 列表按日期倒序时自然沉底，不淹没人工博文（主题 pin 版无
# hiddenFromHomePage 参数；hiddenInHomeList 会连 term 聚合页一起滤掉，不可用）。
DATE_BY_MODULE_ZH_KEY = {
    "core": "2026-07-10", "image_reader": "2026-07-10", "image_writer": "2026-07-10",
    "image_filtering": "2026-07-10",
    "image_geometry": "2026-07-11", "image_color": "2026-07-11",
    "image_color_grading": "2026-07-11",
    "image_enhance": "2026-07-12", "image_segmentation": "2026-07-12",
    "video_io": "2026-07-12",
    "gpu_image_processing": "2026-07-13", "render_task": "2026-07-13",
    "blend": "2026-07-14", "face_detect": "2026-07-14", "matting": "2026-07-14",
}

TAG = "node-guide"
LONG_ENUM = 10  # 选项数超过该值时表格内不展开，改在表后专节列出


def load_module_map():
    """subnode.json: task type -> 模块 key（io_input/io_output 归 core）。"""
    sub = json.loads((REPO / "subnode.json").read_text())
    mapping = {}
    for m in sub["submodules"]:
        for t in m.get("tasks", []):
            mapping[t] = m["name"]
    for t in ("io_input", "io_output"):
        mapping[t] = "core"
    return mapping


def imageish(port):
    tn = port.get("type_name", "")
    return tn == "" or "Mat" in tn or "Image" in tn


def type_display(port):
    tn = port.get("type_name", "")
    return "Image / cv::Mat" if tn == "" else f"`{tn}`"


def fmt_default(p, lang):
    d = p.get("default")
    if p["type"] == "string":
        return "（空）" if d == "" else f"`\"{d}\"`"
    if p["type"] == "float":
        return f"`{d:g}`"
    if p["type"] == "bool":
        return "`true`" if d else "`false`"
    return f"`{d}`"


def param_note(p, meta, common):
    note = ""
    if meta:
        note = meta.get("params", {}).get(p["name"], "")
    if not note:
        note = p.get("description", "") or common.get(p["name"], "")
    if p.get("visible_when"):
        lead = "随 `{}` 联动显示".format(p["visible_when"]) if lang_zh else \
            "shown conditionally on `{}`".format(p["visible_when"])
        note = f"*{lead}。*" + (note or "")
    return note


lang_zh = True  # 模块级开关（param_note 使用）


def params_table(s, meta, common, zh):
    global lang_zh
    lang_zh = zh
    hdr = ("| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |",
           "| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |")
    sep = "|---|---|---|---|---|"
    rows = [hdr[0], sep]
    long_enums = []
    for p in s["params"]:
        opts = ""
        if p["type"] == "enum":
            labels = [e["label"] for e in p.get("enum_values", [])]
            if len(labels) > LONG_ENUM:
                opts = f"{len(labels)} 项（见表后清单）" if zh else f"{len(labels)} options (see list below)"
                long_enums.append((p["name"], labels))
            else:
                opts = " / ".join(f"`{l}`" for l in labels)
        elif p.get("min") is not None:
            def num(v):
                return f"{int(v)}" if float(v).is_integer() else f"{v:g}"
            rng = f"[{num(p['min'])}, {num(p['max'])}]"
            if p.get("step"):
                rng += f"（步长 {p['step']:g}）" if zh else f" (step {p['step']:g})"
            opts = rng
        note = param_note(p, meta, common)
        rows.append(f"| `{p['name']}` | {p['type']} | {fmt_default(p, zh)} | {opts} | {note} |")
    out = "\n".join(rows)
    for name, labels in long_enums:
        title = "选项清单" if zh else "Options"
        items = "\n".join(f"- `{l}`" for l in labels)
        out += f"\n\n**`{name}` {title}**（{len(labels)}）：\n\n{items}"
    return out


def ports_table(ports, direction, zh):
    if not ports:
        return ("*（无）*" if zh else "*None.*")
    rows = ["| {} | {} | {} |".format(
        "端口" if zh else "Port", "数据类型" if zh else "Type",
        "必填" if zh else "Required"), "|---|---|---|"]
    for p in ports:
        req = "✓" if p.get("required") else "—"
        rows.append(f"| `{p['name']}` | {type_display(p)} | {req} |")
    return "\n".join(rows)


def example_graph(task_type, s):
    """image-in/image-out 任务生成可拖进 GraphStudio 的三段示例；否则返回 None。"""
    ins = [p for p in s["inputs"] if imageish(p)]
    outs = [p for p in s["outputs"] if imageish(p)]
    if not ins or not outs:
        return None
    tasks, edges = [], []
    for i, port in enumerate(ins):
        rid = "read" if len(ins) == 1 else f"read{i + 1}"
        tasks.append({"id": rid, "type": "opencv_image_read",
                      "params": {"file_path": "data/test.png"}})
        edges.append({"from": rid, "from_port": "out", "to": "node", "to_port": port["name"]})
    params = {}
    for p in s["params"]:
        if p.get("hidden"):
            continue
        if p["type"] == "string" and p.get("default", "") == "":
            continue
        params[p["name"]] = p.get("default")
    tasks.append({"id": "node", "type": task_type, "params": params})
    for j, port in enumerate(outs[:3]):
        wid = "save" if len(outs[:3]) == 1 else f"save{j + 1}"
        tasks.append({"id": wid, "type": "opencv_image_write",
                      "params": {"file_path": f"out_{port['name']}.png"}})
        edges.append({"from": "node", "from_port": port["name"], "to": wid, "to_port": "in"})
    return {"version": "2.0", "tasks": tasks, "edges": edges}


def render(task_type, s, module_key, zh):
    meta = (META_ZH if zh else META_EN).get(task_type)
    common = COMMON_ZH if zh else COMMON_EN
    modules = MODULES_ZH if zh else MODULES_EN
    mod_name = modules.get(module_key, module_key)
    if meta is None:
        raise SystemExit(f"missing metadata for {task_type} ({'zh' if zh else 'en'})")

    date = DATE_BY_MODULE_ZH_KEY[module_key] + "T10:00:00+08:00"
    lang_prefix = "" if zh else "/en"

    parts = []
    fm = [
        "---",
        f'title: "{meta["title"]}"',
        f"date: {date}",
        f'tags: ["{TAG}"]',
        f'categories: ["{ "节点手册" if zh else "Node Guide" }"]',
        f'summary: "{meta["summary"]}"',
        "showToc: true",
        "---",
        "",
    ]
    parts.append("\n".join(fm))

    body = [meta["desc"], "",
            f'**{"所属模块" if zh else "Module"}**：{mod_name} · '
            f'**{"任务类型" if zh else "Task type"}**：`{task_type}`', ""]
    parts.append("\n".join(body))

    parts.append(f"## {'端口' if zh else 'Ports'}\n\n"
                 f"**{'输入' if zh else 'Inputs'}**\n\n{ports_table(s['inputs'], 'in', zh)}\n\n"
                 f"**{'输出' if zh else 'Outputs'}**\n\n{ports_table(s['outputs'], 'out', zh)}")

    parts.append(f"## {'参数' if zh else 'Parameters'}\n\n{params_table(s, meta, common, zh)}")

    eg = example_graph(task_type, s)
    if eg:
        code = json.dumps(eg, ensure_ascii=False, indent=2)
        tip = ("示例图为最小可运行流水线（读取 → 处理 → 写出），可直接拖入 "
               "GraphStudio 或保存为 `.json` 后用 `--open` 打开；`data/test.png` "
               "替换为实际图像路径。" if zh else
               "A minimal runnable pipeline (read → process → write). Drag it into "
               "GraphStudio or open via `--open`; replace `data/test.png` with a real path.")
        parts.append(f"## {'示例图' if zh else 'Example graph'}\n\n{tip}\n\n```json\n{code}\n```")

    notes = meta.get("notes") or []
    if notes:
        items = "\n".join(f"- {n}" for n in notes)
        parts.append(f"## {'注意事项' if zh else 'Notes'}\n\n{items}")

    footer = (f"> 本文是 [GraphStudio 节点手册]({lang_prefix}/tags/{TAG}/) "
              f"系列文章之一；参数与行为以最新发布版为准。对应英文版见页面语言切换。" if zh else
              f"> Part of the [GraphStudio node guide]({lang_prefix}/tags/{TAG}/) "
              f"series; parameters reflect the latest release.")
    parts.append("---\n\n" + footer + "\n")
    return "\n\n".join(parts)


def main():
    specs = json.loads((HERE / "task_specs.json").read_text())
    module_map = load_module_map()
    if "--list" in sys.argv:
        for s in specs:
            print(f"{s['type']:36s} {module_map.get(s['type'], '?'):22s} "
                  f"{len(s['params'])} params")
        print(f"total: {len(specs)}")
        return

    written = 0
    for lang_dir, zh in (("zh", True), ("en", False)):
        for s in specs:
            t = s["type"]
            module_key = module_map.get(t)
            if module_key is None:
                raise SystemExit(f"task {t} not in subnode.json (update generator mapping)")
            content = render(t, s, module_key, zh)
            out = REPO / "docs" / "content" / lang_dir / "blog" / t / "index.md"
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_text(content)
            written += 1
    print(f"wrote {written} pages under docs/content/{{zh,en}}/blog/")


if __name__ == "__main__":
    main()
