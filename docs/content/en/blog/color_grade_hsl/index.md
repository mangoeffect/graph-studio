---
title: "color_grade_hsl · HSL qualifier"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Per-hue-range (red/yellow/green/cyan/blue/magenta) hue/sat/lum adjustments plus master controls."
showToc: true
---


HSL-zoned grading: pick a hue range, then shift its hue, saturation and luminance independently; master saturation/luminance ride along. Six zones × 3 components are packed into the adjustments string.

**Module**：Color grading · **Task type**：`color_grade_hsl`


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
| `hue_low` | float | `0` | [0, 360] |  |
| `hue_high` | float | `360` | [0, 360] |  |
| `sat_low` | float | `0` | [0, 1] |  |
| `sat_high` | float | `1` | [0, 1] |  |
| `val_low` | float | `0` | [0, 1] |  |
| `val_high` | float | `1` | [0, 1] |  |
| `softness` | float | `0` | [0, 1] |  |
| `lift_r` | float | `0` | [-1, 1] |  |
| `lift_g` | float | `0` | [-1, 1] |  |
| `lift_b` | float | `0` | [-1, 1] |  |
| `gamma_r` | float | `1` | [0.1, 4] |  |
| `gamma_g` | float | `1` | [0.1, 4] |  |
| `gamma_b` | float | `1` | [0.1, 4] |  |
| `gain_r` | float | `1` | [0, 4] |  |
| `gain_g` | float | `1` | [0, 4] |  |
| `gain_b` | float | `1` | [0, 4] |  |

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
      "type": "color_grade_hsl",
      "params": {
        "hue_low": 0,
        "hue_high": 360,
        "sat_low": 0,
        "sat_high": 1,
        "val_low": 0,
        "val_high": 1,
        "softness": 0,
        "lift_r": 0,
        "lift_g": 0,
        "lift_b": 0,
        "gamma_r": 1,
        "gamma_g": 1,
        "gamma_b": 1,
        "gain_r": 1,
        "gain_g": 1,
        "gain_b": 1
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
