---
title: "opencv_clahe · CLAHE"
date: 2026-07-12T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Contrast-limited adaptive histogram equalization: local enhancement with controllable noise amplification."
showToc: true
---


CLAHE (`cv::createCLAHE`). Equalizes per `tile` grid with `clip_limit` capping the histogram to keep noise down — the workhorse for medical and night-scene enhancement.

**Module**：OpenCV enhancement · **Task type**：`opencv_clahe`


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
| `clip_limit` | float | `2` | [0.1, 40] | contrast limiting strength (1-4 typical) |
| `tile_x` | int | `8` | [1, 64] |  |
| `tile_y` | int | `8` | [1, 64] |  |

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
      "type": "opencv_clahe",
      "params": {
        "clip_limit": 2,
        "tile_x": 8,
        "tile_y": 8
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
