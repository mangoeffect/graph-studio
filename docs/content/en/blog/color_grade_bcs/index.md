---
title: "color_grade_bcs · Brightness/Contrast/Saturation"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Brightness, contrast and saturation in a single node."
showToc: true
---


The classic BCS trio: `brightness` shifts levels, `contrast` stretches them apart, `saturation` adjusts color intensity. Neutral values follow the defaults in the table.

**Module**：Color grading · **Task type**：`color_grade_bcs`


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
| `brightness` | float | `0` | [-1, 1] |  |
| `contrast` | float | `1` | [0, 3] |  |
| `saturation` | float | `1` | [0, 3] |  |

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
      "type": "color_grade_bcs",
      "params": {
        "brightness": 0,
        "contrast": 1,
        "saturation": 1
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
