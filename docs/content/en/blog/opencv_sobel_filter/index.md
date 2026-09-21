---
title: "opencv_sobel_filter · Sobel derivative"
date: 2026-07-10T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Sobel gradient with selectable dx/dy orders — the basic edge-detection building block."
showToc: true
---


Sobel derivative (`cv::Sobel`). `dx`/`dy` pick direction and order (their sum must be below the kernel size); the magnitude output typically feeds `opencv_threshold` for binarized edges.

**Module**：OpenCV filtering · **Task type**：`opencv_sobel_filter`


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
| `kernel_size` | int | `3` | [1, 7] (step 2) | kernel edge length in pixels; must be a positive odd number |
| `dx` | int | `1` | [0, 6] | derivative order in x |
| `dy` | int | `1` | [0, 6] | derivative order in y |

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
      "type": "opencv_sobel_filter",
      "params": {
        "kernel_size": 3,
        "dx": 1,
        "dy": 1
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
