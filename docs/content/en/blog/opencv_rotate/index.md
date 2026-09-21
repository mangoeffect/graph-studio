---
title: "opencv_rotate · Rotate 90°"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Fast 90°/180°/270° rotation (rotate code 0/1/2)."
showToc: true
---


Right-angle rotation (`cv::rotate`). Only multiples of 90°, with zero interpolation loss; arbitrary angles go through `opencv_warp_affine`.

**Module**：OpenCV geometry · **Task type**：`opencv_rotate`


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
| `rotate_code` | enum | `0` | `90_CW` / `180` / `90_CCW` | rotation: 0 = 90° clockwise, 1 = 180°, 2 = 270° clockwise |

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
      "type": "opencv_rotate",
      "params": {
        "rotate_code": 0
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
