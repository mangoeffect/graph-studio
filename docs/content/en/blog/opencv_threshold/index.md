---
title: "opencv_threshold · Threshold"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Global threshold with binary/inverted/trunc/tozero semantics, plus OTSU and TRIANGLE auto modes."
showToc: true
---


Fixed threshold (`cv::threshold`). `type` picks binary/inverted/truncate/to-zero semantics; `OTSU_BINARY` / `TRIANGLE_BINARY` ignore `thresh` and derive the threshold automatically.

**Module**：OpenCV color & threshold · **Task type**：`opencv_threshold`


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
| `thresh` | float | `128` | [0, 255] |  |
| `maxval` | float | `255` | [0, 255] |  |
| `type` | enum | `0` | `BINARY` / `BINARY_INV` / `TRUNC` / `TOZERO` / `TOZERO_INV` / `OTSU_BINARY` / `TRIANGLE_BINARY` |  |

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
      "type": "opencv_threshold",
      "params": {
        "thresh": 128,
        "maxval": 255,
        "type": 0
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

- OTSU/TRIANGLE ignore `thresh`; the input must be 8-bit single channel (grayscale first).

---

> Part of the [GraphStudio node guide](/en/tags/node-guide/) series; parameters reflect the latest release.
