---
title: "color_grade_wheels · 色轮调色"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "Lift / Gamma / Gain 三色轮：阴影、中间调、高光分别做 RGB 偏移与整体明度调整。"
showToc: true
---


按 DaVinci 风格三色轮调色：`lift`（阴影）、`gamma`（中间调）、`gain`（高光）各自由 RGB 偏移 + 明度构成。字符串按 RGB + 亮度四分量给出，负值偏青、正值偏暖（数值含义为乘性/加性组合，见参数表默认形态）。

**所属模块**：调色（Color Grading） · **任务类型**：`color_grade_wheels`


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
| `lift_r` | float | `0` | [-1, 1] |  |
| `lift_g` | float | `0` | [-1, 1] |  |
| `lift_b` | float | `0` | [-1, 1] |  |
| `gamma_r` | float | `1` | [0.1, 4] |  |
| `gamma_g` | float | `1` | [0.1, 4] |  |
| `gamma_b` | float | `1` | [0.1, 4] |  |
| `gain_r` | float | `1` | [0, 4] |  |
| `gain_g` | float | `1` | [0, 4] |  |
| `gain_b` | float | `1` | [0, 4] |  |
| `offset_r` | float | `0` | [-1, 1] |  |
| `offset_g` | float | `0` | [-1, 1] |  |
| `offset_b` | float | `0` | [-1, 1] |  |

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
      "type": "color_grade_wheels",
      "params": {
        "lift_r": 0,
        "lift_g": 0,
        "lift_b": 0,
        "gamma_r": 1,
        "gamma_g": 1,
        "gamma_b": 1,
        "gain_r": 1,
        "gain_g": 1,
        "gain_b": 1,
        "offset_r": 0,
        "offset_g": 0,
        "offset_b": 0
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
