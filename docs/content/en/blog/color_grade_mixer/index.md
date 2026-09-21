---
title: "color_grade_mixer · Channel mixer"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Recombines RGB output channels from weighted input channels — channel swaps and tonal splits."
showToc: true
---


Channel mixer: each output channel is a weighted sum of the R/G/B inputs (a 3×3 weights string). Classic moves include red/cyan swaps, custom black-and-white weights, and pseudo-infrared tones.

**Module**：Color grading · **Task type**：`color_grade_mixer`


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
| `m_rr` | float | `1` | [-2, 2] |  |
| `m_rg` | float | `0` | [-2, 2] |  |
| `m_rb` | float | `0` | [-2, 2] |  |
| `m_gr` | float | `0` | [-2, 2] |  |
| `m_gg` | float | `1` | [-2, 2] |  |
| `m_gb` | float | `0` | [-2, 2] |  |
| `m_br` | float | `0` | [-2, 2] |  |
| `m_bg` | float | `0` | [-2, 2] |  |
| `m_bb` | float | `1` | [-2, 2] |  |

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
      "type": "color_grade_mixer",
      "params": {
        "m_rr": 1,
        "m_rg": 0,
        "m_rb": 0,
        "m_gr": 0,
        "m_gg": 1,
        "m_gb": 0,
        "m_br": 0,
        "m_bg": 0,
        "m_bb": 1
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
