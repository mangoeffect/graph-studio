---
title: "render_pass · Render pass"
date: 2026-07-13T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "The single-pass offscreen building block: one script, one fullscreen-triangle draw."
showToc: true
---


The minimal unit of graph-level render orchestration: one pass = load the effect script → optional clear → one fullscreen-triangle draw. Compose multi-pass work with `render_pipeline`; this node suits single-effect debugging and quick previews.

**Module**：GPU render · **Task type**：`render_pass`


## Ports

**Inputs**

| Port | Type | Required |
|---|---|---|
| `in` | Image / cv::Mat | ✓ |

**Outputs**

| Port | Type | Required |
|---|---|---|
| `out` | `task_graph::Image` | ✓ |

## Parameters

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `effect` | string | （空） |  |  |
| `shader_path` | string | （空） |  |  |
| `effects_path` | string | （空） |  |  |
| `uniform` | string | （空） |  |  |
| `script_path` | string | （空） |  | script path prefix (.metal / .vert+.frag / .wgsl); empty = built-in passthrough |
| `width` | int | `0` | [0, 65535] | target width in pixels; 0 follows the input |
| `height` | int | `0` | [0, 65535] | target height in pixels; 0 follows the input |
| `format` | enum | `0` | `rgba8` / `rgba32f` | render target texture format |
| `clear` | bool | `true` |  | clear the target at pass start |
| `clear_color` | string | `"0,0,0,0"` |  | clear color as comma-separated RGBA components |
| `blend` | bool | `false` |  | enable blended output |

## Example graph

A minimal runnable pipeline (read → process → write). Drag it into GraphStudio or open via `--open`; replace `data/test.png` with a real path.

```json
{
  "version": "2.0",
  "tasks": [
    {
      "id": "read",
      "type": "opencv_image_read",
      "params": {
        "file_path": "data/test.png"
      }
    },
    {
      "id": "node",
      "type": "render_pass",
      "params": {
        "width": 0,
        "height": 0,
        "format": 0,
        "clear": true,
        "clear_color": "0,0,0,0",
        "blend": false
      }
    },
    {
      "id": "save",
      "type": "opencv_image_write",
      "params": {
        "file_path": "out_out.png"
      }
    }
  ],
  "edges": [
    {
      "from": "read",
      "from_port": "out",
      "to": "node",
      "to_port": "in"
    },
    {
      "from": "node",
      "from_port": "out",
      "to": "save",
      "to_port": "in"
    }
  ]
}
```

## Notes

- Output stays GPU-resident and chains into compute/render nodes without downloads.

---

> Part of the [GraphStudio node guide](/en/tags/node-guide/) series; parameters reflect the latest release.
