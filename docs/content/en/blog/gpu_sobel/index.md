---
title: "gpu_sobel · GPU Sobel"
date: 2026-07-13T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "GPU Sobel gradient (magnitude output)."
showToc: true
---


GPU Sobel with magnitude semantics aligned to `convertScaleAbs` (|gx|/|gy| each saturated, then a 0.5-weighted blend). Kernel coefficients are calibrated against the CPU reference; interior error ≤1/255.

**Module**：GPU image processing · **Task type**：`gpu_sobel`


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
      "type": "gpu_sobel",
      "params": {}
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
