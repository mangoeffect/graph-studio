---
title: "opencv_connected_components · Connected components"
date: 2026-07-12T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Labels each connected region of a binary image with an integer id."
showToc: true
---


Connected-component analysis (`cv::connectedComponents`). 8- or 4-connectivity; outputs a per-pixel integer label map. Combine with statistics for counting and small-area noise removal.

**Module**：OpenCV segmentation · **Task type**：`opencv_connected_components`


## Ports

**Inputs**

| Port | Type | Required |
|---|---|---|
| `in` | Image / cv::Mat | ✓ |

**Outputs**

| Port | Type | Required |
|---|---|---|
| `labels` | Image / cv::Mat | — |
| `num_labels` | Image / cv::Mat | — |

## Parameters

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `connectivity` | enum | `8` | `C8` / `C4` | 8 or 4 |

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
      "type": "opencv_connected_components",
      "params": {
        "connectivity": 8
      }
    },
    {
      "id": "save1",
      "type": "opencv_image_write",
      "params": {
        "file_path": "out_labels.png"
      }
    },
    {
      "id": "save2",
      "type": "opencv_image_write",
      "params": {
        "file_path": "out_num_labels.png"
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
      "from_port": "labels",
      "to": "save1",
      "to_port": "in"
    },
    {
      "from": "node",
      "from_port": "num_labels",
      "to": "save2",
      "to_port": "in"
    }
  ]
}
```

---

> Part of the [GraphStudio node guide](/en/tags/node-guide/) series; parameters reflect the latest release.
