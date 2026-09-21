---
title: "opencv_sobel_filter · Sobel 导数"
date: 2026-07-10T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "Sobel 算子求图像梯度（dx / dy 阶数可选），边缘检测的基本积木。"
showToc: true
---


Sobel 导数（`cv::Sobel`）。`dx`/`dy` 指定求导方向与阶数（和须小于核尺寸）；输出幅度图，常接 `opencv_threshold` 二值化得到边缘。

**所属模块**：OpenCV 图像滤波 · **任务类型**：`opencv_sobel_filter`


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
| `kernel_size` | int | `3` | [1, 7]（步长 2） | 核边长（像素），须为正奇数 |
| `dx` | int | `1` | [0, 6] | x 方向求导阶数 |
| `dy` | int | `1` | [0, 6] | y 方向求导阶数 |

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
      "type": "opencv_sobel_filter",
      "params": {
        "kernel_size": 3,
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

> 本文是 [GraphStudio 节点手册](/tags/node-guide/) 系列文章之一；参数与行为以最新发布版为准。对应英文版见页面语言切换。
