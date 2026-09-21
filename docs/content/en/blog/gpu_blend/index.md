---
title: "gpu_blend · GPU two-input blend"
date: 2026-07-13T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Linear blend of two inputs on the GPU (opacity interpolation)."
showToc: true
---


GPU two-input linear blend: `out = mix(in, in2, opacity)`. This is NOT the same node as `blend` (the Photoshop 27-mode submodule) — that one is richer and carries a CPU fallback.

**Module**：GPU image processing · **Task type**：`gpu_blend`


## Ports

**Inputs**

| Port | Type | Required |
|---|---|---|
| `in` | Image / cv::Mat | ✓ |
| `in2` | Image / cv::Mat | ✓ |

**Outputs**

| Port | Type | Required |
|---|---|---|
| `out` | `task_graph::Image` | ✓ |

## Parameters

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `w1` | float | `0.5` | [0, 1] |  |
| `w2` | float | `0.5` | [0, 1] |  |

## Example graph

A minimal runnable pipeline (read → process → write). Drag it into GraphStudio or open via `--open`; replace `data/test.png` with a real path.

```json
{
  "version": "2.0",
  "tasks": [
    {
      "id": "read1",
      "type": "opencv_image_read",
      "params": {
        "file_path": "data/test.png"
      }
    },
    {
      "id": "read2",
      "type": "opencv_image_read",
      "params": {
        "file_path": "data/test.png"
      }
    },
    {
      "id": "node",
      "type": "gpu_blend",
      "params": {
        "w1": 0.5,
        "w2": 0.5
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
      "from": "read1",
      "from_port": "out",
      "to": "node",
      "to_port": "in"
    },
    {
      "from": "read2",
      "from_port": "out",
      "to": "node",
      "to_port": "in2"
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
