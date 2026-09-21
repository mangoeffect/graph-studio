---
title: "face_detect · Face detection"
date: 2026-07-14T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Face box detection with optional 478-point landmarks; MediaPipe/MNN backends with auto fallback."
showToc: true
---


Detects faces in the input image and outputs boxes; enabling `output_landmarks` adds 478-point landmarks (mediapipe_478 scheme, unified across backends). With `backend` = auto, MediaPipe runs first and MNN takes over when unavailable; the backend actually used is observable in the result.

**Module**：Face detection · **Task type**：`face_detect`


## Ports

**Inputs**

| Port | Type | Required |
|---|---|---|
| `in` | Image / cv::Mat | ✓ |

**Outputs**

| Port | Type | Required |
|---|---|---|
| `out` | Image / cv::Mat | — |

## Parameters

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `backend` | enum | `0` | `Auto` / `MediaPipe` / `MNN` | inference backend; Auto degrades gracefully (MediaPipe → MNN) |
| `model_path` | string | （空） |  | *shown conditionally on `backend`。*MNN detector model path (empty = built-in search path) |
| `landmark_model_path` | string | （空） |  | *shown conditionally on `backend`。*landmark model path (used with output_landmarks) |
| `output_landmarks` | bool | `false` |  | emit 478-point landmarks |
| `max_faces` | int | `5` | [1, 10] | maximum faces kept |
| `score_threshold` | float | `0.5` | [0, 1] | confidence threshold |
| `nms_threshold` | float | `0.3` | [0, 1] | NMS overlap threshold |
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
      "type": "face_detect",
      "params": {
        "backend": 0,
        "output_landmarks": false,
        "max_faces": 5,
        "score_threshold": 0.5,
        "nms_threshold": 0.3,
        "delegate": 0,
        "device": 2,
        "threads": 4
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

- Missing model assets fail the task with a pointer to the download script — never silent empty results.
- TG_FACE_DEBUG=1 enables backend diagnostics.

---

> Part of the [GraphStudio node guide](/en/tags/node-guide/) series; parameters reflect the latest release.
