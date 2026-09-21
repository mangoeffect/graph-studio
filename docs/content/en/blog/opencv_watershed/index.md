---
title: "opencv_watershed · Watershed"
date: 2026-07-12T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Classic watershed with a marker image to split touching objects."
showToc: true
---


Watershed (`cv::watershed`). Needs the input image plus a marker map (distinct integer labels as seeds, 0 = unknown); flooding over the gradient terrain yields boundaries. Upstream, `opencv_threshold` + `opencv_connected_components` typically build the markers.

**Module**：OpenCV segmentation · **Task type**：`opencv_watershed`


## Ports

**Inputs**

| Port | Type | Required |
|---|---|---|
| `in` | Image / cv::Mat | ✓ |
| `markers` | Image / cv::Mat | — |

**Outputs**

| Port | Type | Required |
|---|---|---|
| `out` | `cv::Mat` | ✓ |

## Parameters

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `rect_x` | int | `-1` | [-1, 16384] |  |
| `rect_y` | int | `-1` | [-1, 16384] |  |
| `rect_width` | int | `-1` | [-1, 16384] |  |
| `rect_height` | int | `-1` | [-1, 16384] |  |

## Example graph

A minimal runnable pipeline (read → process → write). Drag it into GraphStudio or open via `--open`; replace `data/test.png` with a real path.

```json
{
  "version": "2.0",
  "tasks": [
    {
      "id": "read1",
      "type": "opencv_image_read",
      "params": {
        "file_path": "data/test.png"
      }
    },
    {
      "id": "read2",
      "type": "opencv_image_read",
      "params": {
        "file_path": "data/test.png"
      }
    },
    {
      "id": "node",
      "type": "opencv_watershed",
      "params": {
        "rect_x": -1,
        "rect_y": -1,
        "rect_width": -1,
        "rect_height": -1
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
      "from": "read1",
      "from_port": "out",
      "to": "node",
      "to_port": "in"
    },
    {
      "from": "read2",
      "from_port": "out",
      "to": "node",
      "to_port": "markers"
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
