---
title: "render_pipeline · Multi-pass render pipeline"
date: 2026-07-13T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Task-level pass orchestration: a passes count plus flat pass{i}_* parameter keys express the whole filter chain."
showToc: true
---


Chains N render passes inside one task: `passes` declares the count; each pass's effect and parameters are flat keys like `pass1_effect`, `pass1_intensity` … (TaskParams is a flat map — nested structures don't fit). Composite filters (2D Gaussian = horizontal + vertical; morphological open/close = erode + dilate) are expressed this way rather than as new composite task types.

**Module**：GPU render · **Task type**：`render_pipeline`


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
| `passes` | int | `2` | [1, 64] | number of passes (1-64) |
| `width` | int | `0` | [0, 65535] | output canvas width; 0 follows the input |
| `height` | int | `0` | [0, 65535] | output canvas height; 0 follows the input |
| `effects_path` | string | （空） |  | effect manifest (directory) path; empty for built-ins |

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
      "type": "render_pipeline",
      "params": {
        "passes": 2,
        "width": 0,
        "height": 0
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

- pass{i}_effect names the effect for each segment; other pass{i}_<param> keys pass through.
- Runnable example graphs live in `submodules/render/render_task/tests/graphs/`.

---

> Part of the [GraphStudio node guide](/en/tags/node-guide/) series; parameters reflect the latest release.
