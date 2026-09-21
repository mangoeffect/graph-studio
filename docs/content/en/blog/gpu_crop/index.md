---
title: "gpu_crop · GPU crop"
date: 2026-07-13T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Rectangular crop on the GPU (x/y/width/height)."
showToc: true
---


GPU crop: extracts the (x, y, width, height) rectangle; the output size follows.

**Module**：GPU image processing · **Task type**：`gpu_crop`


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
| `x` | int | `0` | [0, 16384] |  |
| `y` | int | `0` | [0, 16384] |  |
| `w` | int | `64` | [1, 16384] |  |
| `h` | int | `64` | [1, 16384] |  |

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
      "type": "gpu_crop",
      "params": {
        "x": 0,
        "y": 0,
        "w": 64,
        "h": 64
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
