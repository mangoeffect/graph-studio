---
title: "opencv_image_write · Write image"
date: 2026-07-10T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Writes the upstream image to a file; the format is inferred from the extension."
showToc: true
---


Writes the image received on the input port to `file_path`. The output format follows the file extension; alpha survives in formats that support it (e.g. PNG).

**Module**：Image output · **Task type**：`opencv_image_write`


## Ports

**Inputs**

| Port | Type | Required |
|---|---|---|
| `in` | Image / cv::Mat | ✓ |

**Outputs**

*None.*

## Parameters

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `file_path` | string | （空） |  | output path; relative paths join the graph directory directly (no asset probing, so original assets can't be clobbered) |

---

> Part of the [GraphStudio node guide](/en/tags/node-guide/) series; parameters reflect the latest release.
