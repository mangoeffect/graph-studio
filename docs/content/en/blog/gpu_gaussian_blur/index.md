---
title: "gpu_gaussian_blur · GPU Gaussian blur"
date: 2026-07-13T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Gaussian blur as a GPU compute op (WGSL/MSL/GLSL single-source kernels)."
showToc: true
---


Gaussian blur on GPU, semantics aligned with `opencv_gaussian_blur_filter`. Output stays GPU-resident and chains into other gpu_* nodes with zero copies.

**Module**：GPU image processing · **Task type**：`gpu_gaussian_blur`


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
| `kernel_size` | int | `5` | [1, 99] (step 2) | kernel edge length in pixels; must be a positive odd number |
| `sigma` | float | `0` | [0, 100] | Gaussian sigma; 0 derives it from the kernel size |

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
      "type": "gpu_gaussian_blur",
      "params": {
        "kernel_size": 5,
        "sigma": 0
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
