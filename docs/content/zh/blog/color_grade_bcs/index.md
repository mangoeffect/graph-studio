---
title: "color_grade_bcs · 亮度/对比度/饱和度"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "一次节点同时调亮度、对比度、饱和度。"
showToc: true
---


经典 BCS 三连调：`brightness` 加减明度、`contrast` 拉开层次、`saturation` 调色彩浓度。所有参数以 0 为中性（或 1 为乘性中性，见默认值）。

**所属模块**：调色（Color Grading） · **任务类型**：`color_grade_bcs`


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
| `brightness` | float | `0` | [-1, 1] |  |
| `contrast` | float | `1` | [0, 3] |  |
| `saturation` | float | `1` | [0, 3] |  |

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
      "type": "color_grade_bcs",
      "params": {
        "brightness": 0,
        "contrast": 1,
        "saturation": 1
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
