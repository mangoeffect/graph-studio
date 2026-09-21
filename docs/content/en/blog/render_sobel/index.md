---
title: "render_sobel · Render Sobel"
date: 2026-07-13T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Render-path Sobel (ksize 1-7); kernel coefficients computed on the CPU and sent as uniforms."
showToc: true
---


Render-pipeline Sobel: coefficients calibrated to the OpenCV impulse response (derivative kernel = conv([1,0,-1], binomial(k-3))) computed on the CPU, executed by the shader's generic weighted loop. Interior error vs OpenCV ≤1.

**Module**：GPU render · **Task type**：`render_sobel`


## Ports

**Inputs**

| Port | Type | Required |
|---|---|---|
| `in` | Image / cv::Mat | ✓ |

**Outputs**

| Port | Type | Required |
|---|---|---|
| `out` | `task_graph::Image` | ✓ |

## Parameters

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `script_path` | string | （空） |  | custom render script path (from an effect manifest; empty = built-in effects) |
| `width` | int | `0` | [0, 65535] | target width in pixels; 0 follows the input |
| `height` | int | `0` | [0, 65535] | target height in pixels; 0 follows the input |
| `format` | enum | `0` | `rgba8` / `rgba32f` | render target texture format |
| `clear` | bool | `true` |  | clear the target at pass start |
| `clear_color` | string | `"0,0,0,0"` |  | clear color as comma-separated RGBA components |
| `blend` | bool | `false` |  | enable blended output |
| `ksize` | int | `3` | [1, 7] (step 2) | kernel size 1/3/5/7 |
| `dx` | int | `1` | [0, 1] | derivative order in x |
| `dy` | int | `1` | [0, 1] | derivative order in y |

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
      "type": "render_sobel",
      "params": {
        "width": 0,
        "height": 0,
        "format": 0,
        "clear": true,
        "clear_color": "0,0,0,0",
        "blend": false,
        "ksize": 3,
        "dx": 1,
        "dy": 1
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
