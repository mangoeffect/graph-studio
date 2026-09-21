---
title: "video_writer · Write video"
date: 2026-07-12T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Encodes an incoming frame sequence into a video file (fourcc / fps / color configurable)."
showToc: true
---


Video sink node: each upstream frame is written; the file closes and the trailer is flushed when the graph run finishes. `fourcc` is a four-character codec name (e.g. `mp4v`); `fps` must match the source.

**Module**：Video I/O · **Task type**：`video_writer`


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
| `file_path` | string | （空） |  | output path (extension picks the container) |
| `fourcc` | string | `"mp4v"` |  | four-character codec id, e.g. mp4v / avc1 |
| `fps` | float | `30` | [1, 240] | frame rate; mismatch with the source speeds up/slows down playback |
| `is_color` | bool | `true` |  | write color frames |

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
      "type": "video_writer",
      "params": {
        "fourcc": "mp4v",
        "fps": 30,
        "is_color": true
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
