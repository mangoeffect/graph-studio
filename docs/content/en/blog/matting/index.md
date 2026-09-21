---
title: "matting · Portrait matting"
date: 2026-07-14T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Portrait alpha estimation with three outputs (result / gray mask / transparent cutout); MediaPipe/MNN backends."
showToc: true
---


Estimates the portrait alpha channel with three output ports: `out` is the structured result (alpha + backend info), `mask` the 0-255 gray mask, and `cutout` the RGBA transparent cutout (alpha passthrough, background transparent). Alpha is bilinearly rescaled back to the input size. `backend` semantics match face_detect: MediaPipe (selfie segmenter) first, MNN (MODNet) fallback.

**Module**：Image matting · **Task type**：`matting`


## Ports

**Inputs**

| Port | Type | Required |
|---|---|---|
| `in` | Image / cv::Mat | ✓ |

**Outputs**

| Port | Type | Required |
|---|---|---|
| `out` | Image / cv::Mat | — |
| `mask` | Image / cv::Mat | — |
| `cutout` | Image / cv::Mat | — |

## Parameters

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `backend` | enum | `0` | `Auto` / `MediaPipe` / `MNN` | inference backend; Auto degrades gracefully (MediaPipe → MNN) |
| `model_path` | string | （空） |  | *shown conditionally on `backend`。* |
| `delegate` | enum | `0` | `CPU` / `GPU` | *shown conditionally on `backend`。*MediaPipe inference delegate (CPU / GPU) |
| `device` | enum | `2` | `CPU` / `Metal` / `Auto` | *shown conditionally on `backend`。*MNN device (CPU / Metal / Auto) |
| `threads` | int | `4` | [1, 16] | *shown conditionally on `backend`。*CPU inference threads |

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
      "type": "matting",
      "params": {
        "backend": 0,
        "delegate": 0,
        "device": 2,
        "threads": 4
      }
    },
    {
      "id": "save1",
      "type": "opencv_image_write",
      "params": {
        "file_path": "out_out.png"
      }
    },
    {
      "id": "save2",
      "type": "opencv_image_write",
      "params": {
        "file_path": "out_mask.png"
      }
    },
    {
      "id": "save3",
      "type": "opencv_image_write",
      "params": {
        "file_path": "out_cutout.png"
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
      "to": "save1",
      "to_port": "in"
    },
    {
      "from": "node",
      "from_port": "mask",
      "to": "save2",
      "to_port": "in"
    },
    {
      "from": "node",
      "from_port": "cutout",
      "to": "save3",
      "to_port": "in"
    }
  ]
}
```

## Notes

- `cutout` feeds `gpu_alpha_composite` directly (in) over a background (in2) for final compositing.
- Both backends agree on foreground coverage for standard portraits (within ~0.1%).
- TG_MATTING_DEBUG=1 enables MNN diagnostics.

---

> Part of the [GraphStudio node guide](/en/tags/node-guide/) series; parameters reflect the latest release.
