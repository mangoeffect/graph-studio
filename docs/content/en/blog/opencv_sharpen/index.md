---
title: "opencv_sharpen · Sharpen"
date: 2026-07-12T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Classic unsharp-mask sharpening wrapped in one node."
showToc: true
---


Sharpening filter: the classic USM — original + amount × (original − blurred). `amount` sets the strength; over-sharpening amplifies noise and halos.

**Module**：OpenCV enhancement · **Task type**：`opencv_sharpen`


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
| `amount` | float | `1.5` | [0.01, 10] | sharpen strength |
| `sigma` | float | `1` | [0.1, 20] | Gaussian sigma; 0 derives it from the kernel size |

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
      "type": "opencv_sharpen",
      "params": {
        "amount": 1.5,
        "sigma": 1
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
