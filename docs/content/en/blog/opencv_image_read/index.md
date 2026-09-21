---
title: "opencv_image_read · Read image"
date: 2026-07-10T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Reads one image from disk (png / jpg / bmp / tiff / webp …) as the graph's input source."
showToc: true
---


Reads an image and outputs it on `out` — the standard graph input source, supporting every format the OpenCV codecs cover. Output is BGR by default; enabling `keep_alpha` keeps the alpha channel (4-channel RGBA) for blend/matting downstream tasks.

**Module**：Image input · **Task type**：`opencv_image_read`


## Ports

**Inputs**

*None.*

**Outputs**

| Port | Type | Required |
|---|---|---|
| `out` | `cv::Mat` | ✓ |

## Parameters

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `file_path` | string | （空） |  | image path; relative paths resolve against the graph directory (probing it and its ancestors), absolute paths are used as-is |
| `keep_alpha` | bool | `false` |  |  |

## Notes

- A missing path or decode failure fails the task with a readable reason in the log panel.

---

> Part of the [GraphStudio node guide](/en/tags/node-guide/) series; parameters reflect the latest release.
