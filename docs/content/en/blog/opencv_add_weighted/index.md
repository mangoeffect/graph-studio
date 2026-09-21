---
title: "opencv_add_weighted · Weighted blend"
date: 2026-07-12T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Linear blend of two inputs by alpha/beta/gamma — stacking, crossfades, watermarks."
showToc: true
---


Linear blend (`cv::addWeighted`): `out = alpha·in + beta·in2 + gamma`. Two input ports; typical for exposure blending, watermark overlays and animated transitions.

**Module**：OpenCV enhancement · **Task type**：`opencv_add_weighted`


## Ports

**Inputs**

| Port | Type | Required |
|---|---|---|
| `in1` | Image / cv::Mat | ✓ |
| `in2` | Image / cv::Mat | ✓ |

**Outputs**

| Port | Type | Required |
|---|---|---|
| `out` | `cv::Mat` | ✓ |

## Parameters

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `alpha` | float | `0.5` | [-10, 10] | weight of the first input |
| `beta` | float | `0.5` | [-10, 10] | weight of the second input |
| `gamma` | float | `0` | [-255, 255] | additive offset |

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
      "type": "opencv_add_weighted",
      "params": {
        "alpha": 0.5,
        "beta": 0.5,
        "gamma": 0
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
      "to_port": "in1"
    },
    {
      "from": "read2",
      "from_port": "out",
      "to": "node",
      "to_port": "in2"
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

- Both inputs need identical size and channel count.

---

> Part of the [GraphStudio node guide](/en/tags/node-guide/) series; parameters reflect the latest release.
