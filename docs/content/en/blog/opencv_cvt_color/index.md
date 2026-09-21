---
title: "opencv_cvt_color · Color conversion"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Common conversions among BGR/GRAY/RGB/HSV/HLS/LAB/LUV/YCrCb/XYZ."
showToc: true
---


Color-space conversion (`cv::cvtColor`), direction picked by the `code` enum. Convert to HSV before `opencv_threshold` to segment by brightness/saturation — a permanent fixture in grading and masking flows.

**Module**：OpenCV color & threshold · **Task type**：`opencv_cvt_color`


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
| `code` | enum | `6` | 12 options (see list below) |  |

**`code` Options**（12）：

- `BGR2GRAY`
- `GRAY2BGR`
- `BGR2RGB`
- `RGB2GRAY`
- `BGR2HSV`
- `HSV2BGR`
- `BGR2HLS`
- `BGR2LAB`
- `BGR2LUV`
- `BGR2YCRCB`
- `YCRCB2BGR`
- `BGR2XYZ`

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
      "type": "opencv_cvt_color",
      "params": {
        "code": 6
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

- On 8-bit images OpenCV's HSV hue range is 0-179, not 0-359.

---

> Part of the [GraphStudio node guide](/en/tags/node-guide/) series; parameters reflect the latest release.
