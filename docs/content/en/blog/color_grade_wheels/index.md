---
title: "color_grade_wheels · Color wheels"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Lift / Gamma / Gain wheels: RGB offset plus master brightness per tonal range."
showToc: true
---


DaVinci-style three-wheel grading: `lift` (shadows), `gamma` (midtones) and `gain` (highlights), each an RGB offset plus a master brightness. Given as comma-separated RGB + luminance components per wheel.

**Module**：Color grading · **Task type**：`color_grade_wheels`


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
| `lift_r` | float | `0` | [-1, 1] |  |
| `lift_g` | float | `0` | [-1, 1] |  |
| `lift_b` | float | `0` | [-1, 1] |  |
| `gamma_r` | float | `1` | [0.1, 4] |  |
| `gamma_g` | float | `1` | [0.1, 4] |  |
| `gamma_b` | float | `1` | [0.1, 4] |  |
| `gain_r` | float | `1` | [0, 4] |  |
| `gain_g` | float | `1` | [0, 4] |  |
| `gain_b` | float | `1` | [0, 4] |  |
| `offset_r` | float | `0` | [-1, 1] |  |
| `offset_g` | float | `0` | [-1, 1] |  |
| `offset_b` | float | `0` | [-1, 1] |  |

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
      "type": "color_grade_wheels",
      "params": {
        "lift_r": 0,
        "lift_g": 0,
        "lift_b": 0,
        "gamma_r": 1,
        "gamma_g": 1,
        "gamma_b": 1,
        "gain_r": 1,
        "gain_g": 1,
        "gain_b": 1,
        "offset_r": 0,
        "offset_g": 0,
        "offset_b": 0
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
