---
title: "opencv_denoise · Non-local means"
date: 2026-07-12T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "fastNlMeansDenoising: patch-similarity denoise with the best detail retention."
showToc: true
---


Non-local means (`cv::fastNlMeansDenoising`). Aggregates similar patches instead of smoothing locally — better Gaussian-noise removal and detail retention than the bilateral filter, at a higher compute cost. Larger `h` denoises harder.

**Module**：OpenCV enhancement · **Task type**：`opencv_denoise`


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
| `h` | float | `3` | [0.1, 100] | filter strength (luminance); start around 10 |
| `h_color` | float | `3` | [0.1, 100] |  |
| `template_window` | int | `7` | [3, 31] |  |
| `search_window` | int | `21` | [3, 65] |  |

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
      "type": "opencv_denoise",
      "params": {
        "h": 3,
        "h_color": 3,
        "template_window": 7,
        "search_window": 21
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
