---
title: "opencv_warp_affine · Affine warp"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Translate/rotate/scale/shear by a 2×3 affine matrix; arbitrary-angle rotation lives here."
showToc: true
---


Affine warp (`cv::warpAffine`). The matrix is a six-number string (row-major, e.g. translation `1,0,20;0,1,10`); anything that preserves parallel lines can be expressed.

**Module**：OpenCV geometry · **Task type**：`opencv_warp_affine`


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
| `angle` | float | `0` | [-360, 360] |  |
| `scale` | float | `1` | [0.01, 32] |  |
| `center_x` | float | `-1` | [-1, 8192] |  |
| `center_y` | float | `-1` | [-1, 8192] |  |
| `output_width` | int | `0` | [0, 8192] |  |
| `output_height` | int | `0` | [0, 8192] |  |
| `interpolation` | enum | `1` | `LINEAR` / `NEAREST` / `CUBIC` / `LANCZOS4` | interpolation method |
| `border_mode` | enum | `0` | `CONSTANT` / `REPLICATE` / `REFLECT` / `REFLECT_101` / `WRAP` |  |

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
      "type": "opencv_warp_affine",
      "params": {
        "angle": 0,
        "scale": 1,
        "center_x": -1,
        "center_y": -1,
        "output_width": 0,
        "output_height": 0,
        "interpolation": 1,
        "border_mode": 0
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
