---
title: "opencv_scharr_filter · Scharr derivative"
date: 2026-07-10T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Scharr operator: more rotationally accurate than Sobel at the 3× kernel size."
showToc: true
---


Scharr derivative (`cv::Scharr`). Only a 3×3 kernel, but with less angular error than same-size Sobel; swap it in when you need precise gradients or contour extraction.

**Module**：OpenCV filtering · **Task type**：`opencv_scharr_filter`


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
| `dx` | int | `1` | [0, 1] | derivative order in x |
| `dy` | int | `1` | [0, 1] | derivative order in y |

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
      "type": "opencv_scharr_filter",
      "params": {
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
