---
title: "opencv_sqr_box_filter · 平方方框滤波"
date: 2026-07-10T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "核内像素平方的均值（归一化），局部能量/方差估计的积木。"
showToc: true
---


平方方框滤波（`cv::sqrBoxFilter`）。输出的每点是核内像素平方的（归一化）和，配合 `opencv_box_filter` 可推出局部方差，用于纹理/噪声强度分析。

**所属模块**：OpenCV 图像滤波 · **任务类型**：`opencv_sqr_box_filter`


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
| `kernel_size` | int | `3` | [1, 31]（步长 2） | 核边长（像素），须为正奇数 |
| `normalize` | bool | `true` |  | 是否按核面积归一化 |

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
      "type": "opencv_sqr_box_filter",
      "params": {
        "kernel_size": 3,
        "normalize": true
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
