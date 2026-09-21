---
title: "render_lut_cube · .cube to LUT image"
date: 2026-07-13T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Reads a .cube 3D LUT and converts it on the CPU into a HALD LUT image (feeds render_lut's in2)."
showToc: true
---


Bakes a `.cube` text LUT into a HALD LUT image (DOMAIN normalization included), output feeding `render_lut`'s `in2` for GPU grading. Same material as `color_grade_lut` (direct CPU apply) via the other path.

**Module**：GPU render · **Task type**：`render_lut_cube`


## Ports

**Inputs**

*None.*

**Outputs**

| Port | Type | Required |
|---|---|---|
| `out` | `task_graph::Image` | ✓ |

## Parameters

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `lut_file` | string | （空） |  |  |

---

> Part of the [GraphStudio node guide](/en/tags/node-guide/) series; parameters reflect the latest release.
