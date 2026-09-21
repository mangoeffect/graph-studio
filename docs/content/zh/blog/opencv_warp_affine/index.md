---
title: "opencv_warp_affine · 仿射变换"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "按 2×3 仿射矩阵做平移/旋转/缩放/错切，任意角度旋转走这里。"
showToc: true
---


仿射变换（`cv::warpAffine`）。变换矩阵以 2×3 六元数字符串给出（行主序，如平移 `1,0,20;0,1,10`）；保持平行关系的变换都能表达。

**所属模块**：OpenCV 几何变换 · **任务类型**：`opencv_warp_affine`


## 端口

**输入**

| 端口 | 数据类型 | 必填 |
|---|---|---|
| `in` | Image / cv::Mat | ✓ |

**输出**

| 端口 | 数据类型 | 必填 |
|---|---|---|
| `out` | `cv::Mat` | ✓ |

## 参数

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `angle` | float | `0` | [-360, 360] |  |
| `scale` | float | `1` | [0.01, 32] |  |
| `center_x` | float | `-1` | [-1, 8192] |  |
| `center_y` | float | `-1` | [-1, 8192] |  |
| `output_width` | int | `0` | [0, 8192] |  |
| `output_height` | int | `0` | [0, 8192] |  |
| `interpolation` | enum | `1` | `LINEAR` / `NEAREST` / `CUBIC` / `LANCZOS4` | 插值方法 |
| `border_mode` | enum | `0` | `CONSTANT` / `REPLICATE` / `REFLECT` / `REFLECT_101` / `WRAP` |  |

## 示例图

示例图为最小可运行流水线（读取 → 处理 → 写出），可直接拖入 GraphStudio 或保存为 `.json` 后用 `--open` 打开；`data/test.png` 替换为实际图像路径。

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
      "type": "opencv_warp_affine",
      "params": {
        "angle": 0,
        "scale": 1,
        "center_x": -1,
        "center_y": -1,
        "output_width": 0,
        "output_height": 0,
        "interpolation": 1,
        "border_mode": 0
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

> 本文是 [GraphStudio 节点手册](/tags/node-guide/) 系列文章之一；参数与行为以最新发布版为准。对应英文版见页面语言切换。
