---
title: "opencv_resize · Resize"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Resize by target width/height and interpolation method."
showToc: true
---


Image resize (`cv::resize`). Use `INTER_AREA` when shrinking (anti-moiré), `INTER_LINEAR`/`INTER_CUBIC` when enlarging; `INTER_NEAREST` preserves exact values (mandatory for masks/label maps).

**Module**：OpenCV geometry · **Task type**：`opencv_resize`


## Ports

**Inputs**

| Port | Type | Required |
|---|---|---|
| `in` | Image / cv::Mat | ✓ |

**Outputs**

| Port | Type | Required |
|---|---|---|
| `out` | `cv::Mat` | ✓ |

## Parameters

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `width` | int | `0` | [0, 8192] | target width in pixels; 0 follows the input |
| `height` | int | `0` | [0, 8192] | target height in pixels; 0 follows the input |
| `scale_x` | float | `1` | [0.01, 32] |  |
| `scale_y` | float | `1` | [0.01, 32] |  |
| `interpolation` | enum | `1` | `LINEAR` / `NEAREST` / `CUBIC` / `LANCZOS4` / `AREA` | interpolation method |

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
      "type": "opencv_resize",
      "params": {
        "width": 0,
        "height": 0,
        "scale_x": 1,
        "scale_y": 1,
        "interpolation": 1
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
