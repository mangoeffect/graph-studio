---
title: "opencv_filter_2d · 自定义卷积"
date: 2026-07-10T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "用任意自定义核做 2D 卷积/相关：把你的核写成数值矩阵即可。"
showToc: true
---


通用 2D 卷积（`cv::filter2D`）。核以字符串形式给出（行内逗号、行间分号，如 `0,-1,0;-1,5,-1;0,-1,0`），适合试验自定义算子而不写代码。

**所属模块**：OpenCV 图像滤波 · **任务类型**：`opencv_filter_2d`


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
| `delta` | float | `0` | [-255, 255] |  |

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
      "type": "opencv_filter_2d",
      "params": {
        "kernel_size": 3,
        "delta": 0
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
