---
title: "opencv_morphology_ex · Morphology combinations"
date: 2026-07-10T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Open/close/gradient/top-hat/black-hat: the composed morphology operations in one node."
showToc: true
---


Composed morphology (`cv::morphologyEx`): open removes white specks, close fills dark holes, gradient gives morphological edges, top-hat/black-hat extract structures brighter/darker than their neighborhood. The GPU render side expresses the same math by orchestrating `render_dilate_dir` + `render_erode_dir` passes.

**Module**：OpenCV filtering · **Task type**：`opencv_morphology_ex`


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
| `operation` | enum | `2` | `ERODE` / `DILATE` / `OPEN` / `CLOSE` / `GRADIENT` / `TOPHAT` / `BLACKHAT` |  |
| `kernel_size` | int | `3` | [1, 31] (step 2) | kernel edge length in pixels; must be a positive odd number |
| `iterations` | int | `1` | [1, 10] | number of repetitions |

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
      "type": "opencv_morphology_ex",
      "params": {
        "operation": 2,
        "kernel_size": 3,
        "iterations": 1
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
