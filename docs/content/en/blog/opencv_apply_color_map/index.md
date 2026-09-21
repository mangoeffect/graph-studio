---
title: "opencv_apply_color_map · Color map"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Maps single-channel input through a preset palette (JET / VIRIDIS / BONE …) — heatmaps in one step."
showToc: true
---


False-color mapping (`cv::applyColorMap`). Grayscale/depth/confidence maps get colored by the `colormap` enum, outputting a 3-channel visualization.

**Module**：OpenCV color & threshold · **Task type**：`opencv_apply_color_map`


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
| `colormap` | enum | `2` | 17 options (see list below) |  |

**`colormap` Options**（17）：

- `AUTUMN`
- `BONE`
- `JET`
- `WINTER`
- `RAINBOW`
- `OCEAN`
- `HOT`
- `HSV`
- `PINK`
- `PARULA`
- `MAGMA`
- `INFERNO`
- `PLASMA`
- `VIRIDIS`
- `CIVIDIS`
- `TWILIGHT`
- `TURBO`

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
      "type": "opencv_apply_color_map",
      "params": {
        "colormap": 2
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
