---
title: "opencv_gaussian_blur_filter · Gaussian blur"
date: 2026-07-10T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Gaussian-weighted blur: natural smoothing, the default choice for denoising."
showToc: true
---


Gaussian blur (`cv::GaussianBlur`). With `sigma` = 0 the standard deviation is derived from the kernel size. Semantically mirrored on the GPU render side by `render_gauss_h` / `render_gauss_v` (separable passes).

**Module**：OpenCV filtering · **Task type**：`opencv_gaussian_blur_filter`


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
      "type": "opencv_gaussian_blur_filter",
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
