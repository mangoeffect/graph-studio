---
title: "opencv_bilateral_filter · Bilateral filter"
date: 2026-07-10T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Edge-preserving denoise: weights by spatial distance and color difference together."
showToc: true
---


Bilateral filter (`cv::bilateralFilter`). `sigma_color` decides how large a color difference still counts as the same color; `sigma_space` sets the spatial reach — together they set the smoothing strength. A staple for skin smoothing and cartoon-like preprocessing.

**Module**：OpenCV filtering · **Task type**：`opencv_bilateral_filter`


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
| `d` | int | `9` | [1, 50] |  |
| `sigma_color` | float | `75` | [0, 300] | color-space filter strength (larger = wider color ranges are averaged) |
| `sigma_space` | float | `75` | [0, 300] | coordinate-space filter strength (larger = farther pixels influence each other) |

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
      "type": "opencv_bilateral_filter",
      "params": {
        "d": 9,
        "sigma_color": 75,
        "sigma_space": 75
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
