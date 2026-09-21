---
title: "render_box_dir · Directional box blur"
date: 2026-07-13T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Parameterized directional box blur (horizontal/vertical pass)."
showToc: true
---


Directional box blur: same shape as `render_gauss_dir` with a mean kernel. Two directions chained = a 2D mean blur.

**Module**：GPU render · **Task type**：`render_box_dir`


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
| `script_path` | string | （空） |  | custom render script path (from an effect manifest; empty = built-in effects) |
| `width` | int | `0` | [0, 65535] | target width in pixels; 0 follows the input |
| `height` | int | `0` | [0, 65535] | target height in pixels; 0 follows the input |
| `format` | enum | `0` | `rgba8` / `rgba32f` | render target texture format |
| `clear` | bool | `true` |  | clear the target at pass start |
| `clear_color` | string | `"0,0,0,0"` |  | clear color as comma-separated RGBA components |
| `blend` | bool | `false` |  | enable blended output |
| `ksize` | int | `5` | [1, 99] (step 2) | kernel edge length in pixels; must be a positive odd number |
| `direction` | enum | `0` | `horizontal` / `vertical` |  |

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
      "type": "render_box_dir",
      "params": {
        "width": 0,
        "height": 0,
        "format": 0,
        "clear": true,
        "clear_color": "0,0,0,0",
        "blend": false,
        "ksize": 5,
        "direction": 0
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

---

> Part of the [GraphStudio node guide](/en/tags/node-guide/) series; parameters reflect the latest release.
