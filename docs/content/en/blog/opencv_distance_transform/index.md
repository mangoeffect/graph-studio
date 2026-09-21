---
title: "opencv_distance_transform · Distance transform"
date: 2026-07-12T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Distance field of foreground pixels to the nearest background — the intermediate for skeletons and width."
showToc: true
---


Distance transform (`cv::distanceTransform`). Each non-zero pixel gets its distance to the nearest zero pixel, with selectable distance type and mask size; the classic pre-pass for watershed seeding and stroke-width estimation.

**Module**：OpenCV segmentation · **Task type**：`opencv_distance_transform`


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
| `distance_type` | enum | `2` | `L2` / `L1` / `C` |  |
| `mask_size` | enum | `0` | `PRECISE` / `3` / `5` |  |

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
      "type": "opencv_distance_transform",
      "params": {
        "distance_type": 2,
        "mask_size": 0
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
