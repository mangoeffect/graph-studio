---
title: "color_grade_lut · LUT grading"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Applies a .cube 3D LUT (trilinear or tetrahedral interpolation)."
showToc: true
---


Reads a 3D LUT file (`.cube`) and applies it. `interpolation` picks trilinear (fast) or tetrahedral (smoother). Photography grading packs ship as .cube files — drop one in.

**Module**：Color grading · **Task type**：`color_grade_lut`


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
| `lut_file` | string | （空） |  | LUT file path (.cube); relative paths probe the graph directory |
| `interpolation` | enum | `0` | `TRILINEAR` / `TETRAHEDRAL` | TRILINEAR / TETRAHEDRAL |

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
      "type": "color_grade_lut",
      "params": {
        "interpolation": 0
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

- The GPU-render equivalents are `render_lut_cube` (.cube → HALD image) + `render_lut` (apply) — faster for bulk work.

---

> Part of the [GraphStudio node guide](/en/tags/node-guide/) series; parameters reflect the latest release.
