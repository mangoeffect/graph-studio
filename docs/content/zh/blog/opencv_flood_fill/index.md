---
title: "opencv_flood_fill · 泛洪填充"
date: 2026-07-12T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "从种子点按相似度泛洪：选区填充 / 魔棒效果。"
showToc: true
---


泛洪填充（`cv::floodFill`）。从 (seed_x, seed_y) 出发，把与种子颜色差异在lo_diff/up_diff 范围内的连通区域替换为指定颜色，返回填充掩膜。

**所属模块**：OpenCV 图像分割 · **任务类型**：`opencv_flood_fill`


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
| `seed_x` | int | `-1` | [0, 16384] | 种子点 x 坐标 |
| `seed_y` | int | `-1` | [0, 16384] | 种子点 y 坐标 |
| `new_value` | int | `255` | [0, 255] | 填充颜色 |
| `lo_diff` | float | `4` | [0, 255] | 向下容差 |
| `up_diff` | float | `4` | [0, 255] | 向上容差 |
| `connectivity` | enum | `4` | `C4` / `C8` |  |

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
      "type": "opencv_flood_fill",
      "params": {
        "seed_x": -1,
        "seed_y": -1,
        "new_value": 255,
        "lo_diff": 4,
        "up_diff": 4,
        "connectivity": 4
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
