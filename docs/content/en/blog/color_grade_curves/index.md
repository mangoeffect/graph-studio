---
title: "color_grade_curves · Curves"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Master and per-channel curves described as control-point strings."
showToc: true
---


Photoshop-style curve grading. Each curve is a control-point list (x,y pairs, comma-separated) interpolated into a 256-entry LUT. The master curve shapes tone, the R/G/B curves shape color cast.

**Module**：Color grading · **Task type**：`color_grade_curves`


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
| `curve_y` | string | `"0,0;1,1"` |  |  |
| `curve_r` | string | `"0,0;1,1"` |  |  |
| `curve_g` | string | `"0,0;1,1"` |  |  |
| `curve_b` | string | `"0,0;1,1"` |  |  |
| `interp` | enum | `0` | `LINEAR` / `CUBIC` |  |

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
      "type": "color_grade_curves",
      "params": {
        "curve_y": "0,0;1,1",
        "curve_r": "0,0;1,1",
        "curve_g": "0,0;1,1",
        "curve_b": "0,0;1,1",
        "interp": 0
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
