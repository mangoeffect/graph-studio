---
title: "gpu_box_blur · GPU box blur"
date: 2026-07-13T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Box mean blur on the GPU compute pipeline, semantics aligned with opencv_blur_filter."
showToc: true
---


Mean blur on the GPU compute backend (wgpu/Metal/Vulkan), bit-aligned with the CPU reference. An order of magnitude faster for bulk processing; parameters match `opencv_blur_filter`.

**Module**：GPU image processing · **Task type**：`gpu_box_blur`


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
      "type": "gpu_box_blur",
      "params": {
        "kernel_size": 5
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

## Notes

- Fails the task when the GPU backend can't init (no silent CPU fallback; use blend's device=auto when you need fallback semantics).

---

> Part of the [GraphStudio node guide](/en/tags/node-guide/) series; parameters reflect the latest release.
