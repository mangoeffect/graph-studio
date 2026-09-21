---
title: "opencv_grabcut · GrabCut"
date: 2026-07-12T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Iterated graph-cut segmentation initialized by a rectangle or mask — the classic interactive cutout."
showToc: true
---


GrabCut (`cv::grabCut`). `init_mode` picks rectangle (a box containing the target) or mask (rough fg/bg marks); iterated energy minimization refines a precise foreground mask. For high-quality portraits prefer the `matting` node.

**Module**：OpenCV segmentation · **Task type**：`opencv_grabcut`


## Ports

**Inputs**

| Port | Type | Required |
|---|---|---|
| `in` | Image / cv::Mat | ✓ |
| `mask` | Image / cv::Mat | — |

**Outputs**

| Port | Type | Required |
|---|---|---|
| `out` | `cv::Mat` | ✓ |

## Parameters

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `init_mode` | enum | `0` | `RECT` / `MASK` |  |
| `rect_x` | int | `0` | [0, 16384] |  |
| `rect_y` | int | `0` | [0, 16384] |  |
| `rect_width` | int | `0` | [0, 16384] |  |
| `rect_height` | int | `0` | [0, 16384] |  |
| `iterations` | int | `5` | [1, 100] | number of repetitions |

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
      "type": "opencv_grabcut",
      "params": {
        "init_mode": 0,
        "rect_x": 0,
        "rect_y": 0,
        "rect_width": 0,
        "rect_height": 0,
        "iterations": 5
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
      "to_port": "mask"
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

- RECT mode uses rect_x/y/width/height; MASK mode ignores them.
- More iterations refine edges and cost time; 5-10 usually suffices.

---

> Part of the [GraphStudio node guide](/en/tags/node-guide/) series; parameters reflect the latest release.
