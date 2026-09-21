---
title: "opencv_gabor_filter · Gabor filter"
date: 2026-07-10T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Directional texture filtering with Gabor kernels — a classic for fingerprint/fabric analysis."
showToc: true
---


Gabor filtering (`cv::getGaborKernel` + filter2D). Builds a sinusoid-modulated Gaussian kernel from wavelength/direction/bandwidth that responds strongest to a specific orientation and frequency; banks of them form texture features.

**Module**：OpenCV filtering · **Task type**：`opencv_gabor_filter`


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
| `kernel_size` | int | `31` | [1, 99] (step 2) | kernel edge length in pixels; must be a positive odd number |
| `sigma` | float | `5` | [0, 50] | Gaussian envelope sigma |
| `theta` | float | `0` | [0, 6.2832] | filter orientation in radians |
| `lambd` | float | `10` | [0, 100] | sinusoid wavelength in pixels |
| `gamma` | float | `0.5` | [0, 5] | spatial aspect ratio |
| `psi` | float | `1.5708` | [0, 6.2832] | phase offset in radians |

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
      "type": "opencv_gabor_filter",
      "params": {
        "kernel_size": 31,
        "sigma": 5,
        "theta": 0,
        "lambd": 10,
        "gamma": 0.5,
        "psi": 1.5708
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
