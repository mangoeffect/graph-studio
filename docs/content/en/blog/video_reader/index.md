---
title: "video_reader · Read video"
date: 2026-07-12T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Reads video files frame by frame (mp4 / mov / avi …); each execution yields one frame."
showToc: true
---


Video source node: the executor drives it frame by frame — each `execute()` outputs the next frame on `out`, with teardown at stream end. `api_preference` forces a decode backend (automatic by default; FFMPEG is the most common).

**Module**：Video I/O · **Task type**：`video_reader`


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
| `file_path` | string | （空） |  | video path; relative paths probe the graph directory |
| `api_preference` | enum | `0` | `DEFAULT` / `FFMPEG` | decode backend: DEFAULT (auto) / FFMPEG |

## Notes

- Frame semantics are executor-driven: one GraphStudio execution = one frame; use embedded SDK execution or a script loop for batch transcoding.

---

> Part of the [GraphStudio node guide](/en/tags/node-guide/) series; parameters reflect the latest release.
