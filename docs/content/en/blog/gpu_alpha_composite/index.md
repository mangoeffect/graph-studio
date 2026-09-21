---
title: "gpu_alpha_composite · GPU alpha composite"
date: 2026-07-13T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "GPU alpha compositing (over): foreground in over background in2."
showToc: true
---


GPU alpha composite: `in` (alpha-carrying foreground) over `in2` (background). Feeds matting/cutout RGBA straight into the final composition.

**Module**：GPU image processing · **Task type**：`gpu_alpha_composite`


## Ports

**Inputs**

| Port | Type | Required |
|---|---|---|
| `in` | Image / cv::Mat | ✓ |
| `in2` | Image / cv::Mat | ✓ |

**Outputs**

| Port | Type | Required |
|---|---|---|
| `out` | `task_graph::Image` | ✓ |

## Parameters

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|

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
      "type": "gpu_alpha_composite",
      "params": {}
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
      "to_port": "in"
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

- `in2` must be 3-channel (BGR); both inputs must match in size.
- Get 4-channel input via `opencv_image_read` keep_alpha or matting's cutout output.

---

> Part of the [GraphStudio node guide](/en/tags/node-guide/) series; parameters reflect the latest release.
