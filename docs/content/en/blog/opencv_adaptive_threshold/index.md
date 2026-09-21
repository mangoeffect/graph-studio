---
title: "opencv_adaptive_threshold · Adaptive threshold"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Binarizes by local neighborhood statistics (mean/Gaussian) — robust to uneven lighting."
showToc: true
---


Adaptive threshold (`cv::adaptiveThreshold`). The per-pixel threshold comes from the neighborhood mean (or Gaussian-weighted mean) minus a constant C — right for document scans and shadowed scenes with lighting gradients.

**Module**：OpenCV color & threshold · **Task type**：`opencv_adaptive_threshold`


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
| `max_value` | float | `255` | [0, 255] |  |
| `block_size` | int | `11` | [3, 99] | neighborhood edge length (positive odd) |
| `C` | float | `2` | [-50, 50] |  |
| `adaptive_method` | enum | `0` | `MEAN_C` / `GAUSSIAN_C` |  |
| `threshold_type` | enum | `0` | `BINARY` / `BINARY_INV` |  |

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
      "type": "opencv_adaptive_threshold",
      "params": {
        "max_value": 255,
        "block_size": 11,
        "C": 2,
        "adaptive_method": 0,
        "threshold_type": 0
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
