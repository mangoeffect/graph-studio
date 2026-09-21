---
title: "opencv_flood_fill · Flood fill"
date: 2026-07-12T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Floods from a seed point by similarity — selections and magic-wand behavior."
showToc: true
---


Flood fill (`cv::floodFill`). From (seed_x, seed_y), replaces the connected region within lo_diff/up_diff color tolerance with `new_value` and reports the filled mask.

**Module**：OpenCV segmentation · **Task type**：`opencv_flood_fill`


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
| `seed_x` | int | `-1` | [0, 16384] | seed point x |
| `seed_y` | int | `-1` | [0, 16384] | seed point y |
| `new_value` | int | `255` | [0, 255] | fill color |
| `lo_diff` | float | `4` | [0, 255] | lower tolerance |
| `up_diff` | float | `4` | [0, 255] | upper tolerance |
| `connectivity` | enum | `4` | `C4` / `C8` |  |

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
      "type": "opencv_flood_fill",
      "params": {
        "seed_x": -1,
        "seed_y": -1,
        "new_value": 255,
        "lo_diff": 4,
        "up_diff": 4,
        "connectivity": 4
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
