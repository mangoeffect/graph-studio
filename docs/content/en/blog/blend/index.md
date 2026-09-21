---
title: "blend · Layer blend (27 modes)"
date: 2026-07-14T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Two-input compositing with all 27 Photoshop blend modes: GPU-first with a mirrored CPU fallback."
showToc: true
---


Blends `in` (upper layer) onto `in2` (lower layer) by `mode`, implementing the full Photoshop set of 27 blend modes (normal/dissolve/darken family/lighten family/overlay family/difference family/HSL family). `opacity` sets overall opacity. With `device` = auto the GPU compute path runs first and falls back to the mirrored CPU implementation on failure — identical results on any environment. Dissolve-mode noise is reproducible via `seed`.

**Module**：Layer blend · **Task type**：`blend`


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
| `mode` | enum | `0` | 27 options (see list below) | blend mode (27 options, listed below the table) |
| `opacity` | float | `1` | [0, 1] | opacity of the blended result |
| `device` | enum | `0` | `auto` / `gpu` / `cpu` | execution device: auto = GPU first, falling back to the mirrored CPU implementation |
| `seed` | int | `42` | [0, 2147480000] | random seed for dissolve-family modes |

**`mode` Options**（27）：

- `normal`
- `dissolve`
- `darken`
- `multiply`
- `color_burn`
- `linear_burn`
- `darker_color`
- `lighten`
- `screen`
- `color_dodge`
- `linear_dodge`
- `lighter_color`
- `overlay`
- `soft_light`
- `hard_light`
- `vivid_light`
- `linear_light`
- `pin_light`
- `hard_mix`
- `difference`
- `exclusion`
- `subtract`
- `divide`
- `hue`
- `saturation`
- `color`
- `luminosity`

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
      "type": "blend",
      "params": {
        "mode": 0,
        "opacity": 1,
        "device": 0,
        "seed": 42
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

- Inputs must match in size; alpha handling follows the mode's semantics when channel counts differ.
- The HSL family (hue/saturation/color/luminosity) follows Photoshop transfer functions.

---

> Part of the [GraphStudio node guide](/en/tags/node-guide/) series; parameters reflect the latest release.
