---
title: "opencv_sharpen · 锐化"
date: 2026-07-12T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "经典反锐化掩模锐化（unsharp mask 一站式封装）。"
showToc: true
---


锐化滤波：原图 + amount ×（原图 - 模糊图）的经典 USM。`amount` 控制强度，过度锐化会放大噪声与光晕。

**所属模块**：OpenCV 图像增强 · **任务类型**：`opencv_sharpen`


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
| `amount` | float | `1.5` | [0.01, 10] | 锐化强度 |
| `sigma` | float | `1` | [0.1, 20] | 高斯标准差；0 表示按核尺寸自动推算 |

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
      "type": "opencv_sharpen",
      "params": {
        "amount": 1.5,
        "sigma": 1
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
