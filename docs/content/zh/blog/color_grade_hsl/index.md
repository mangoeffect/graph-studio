---
title: "color_grade_hsl · HSL 八通道调色"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "按色相分区（红/黄/绿/青/蓝/洋红 + 明度/饱和度）精细调色。"
showToc: true
---


HSL 分区调色：先选色相区间，再分别调该区间的色相偏移、饱和度与明度；另附全图饱和度/明度。字符串按 6 个色相分区 × 3 分量给出。

**所属模块**：调色（Color Grading） · **任务类型**：`color_grade_hsl`


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
| `hue_low` | float | `0` | [0, 360] |  |
| `hue_high` | float | `360` | [0, 360] |  |
| `sat_low` | float | `0` | [0, 1] |  |
| `sat_high` | float | `1` | [0, 1] |  |
| `val_low` | float | `0` | [0, 1] |  |
| `val_high` | float | `1` | [0, 1] |  |
| `softness` | float | `0` | [0, 1] |  |
| `lift_r` | float | `0` | [-1, 1] |  |
| `lift_g` | float | `0` | [-1, 1] |  |
| `lift_b` | float | `0` | [-1, 1] |  |
| `gamma_r` | float | `1` | [0.1, 4] |  |
| `gamma_g` | float | `1` | [0.1, 4] |  |
| `gamma_b` | float | `1` | [0.1, 4] |  |
| `gain_r` | float | `1` | [0, 4] |  |
| `gain_g` | float | `1` | [0, 4] |  |
| `gain_b` | float | `1` | [0, 4] |  |

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
      "type": "color_grade_hsl",
      "params": {
        "hue_low": 0,
        "hue_high": 360,
        "sat_low": 0,
        "sat_high": 1,
        "val_low": 0,
        "val_high": 1,
        "softness": 0,
        "lift_r": 0,
        "lift_g": 0,
        "lift_b": 0,
        "gamma_r": 1,
        "gamma_g": 1,
        "gamma_b": 1,
        "gain_r": 1,
        "gain_g": 1,
        "gain_b": 1
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
