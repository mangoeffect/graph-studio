---
title: "opencv_gaussian_blur_filter · 高斯模糊"
date: 2026-07-10T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "高斯加权模糊：核内按正态分布加权平均，平滑更自然，是去噪/背景虚化的默认选择。"
showToc: true
---


高斯模糊（`cv::GaussianBlur`）。`sigma` 为 0 时按核尺寸自动推算标准差。与 GPU 渲染侧的 `render_gauss_h` / `render_gauss_v` 语义对应，后者在 GPU 上可分离执行。

**所属模块**：OpenCV 图像滤波 · **任务类型**：`opencv_gaussian_blur_filter`


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
| `kernel_size` | int | `5` | [1, 99]（步长 2） | 核边长（像素），须为正奇数 |
| `sigma` | float | `0` | [0, 100] | 高斯标准差；0 表示按核尺寸自动推算 |

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
      "type": "opencv_gaussian_blur_filter",
      "params": {
        "kernel_size": 5,
        "sigma": 0
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
