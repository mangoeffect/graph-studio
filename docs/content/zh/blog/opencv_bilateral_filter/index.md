---
title: "opencv_bilateral_filter · 双边滤波"
date: 2026-07-10T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "保边去噪：同时按空间距离与色彩差异加权，磨皮/去噪的同时保留轮廓。"
showToc: true
---


双边滤波（`cv::bilateralFilter`）。`sigma_color` 控制多大色差被视为『同一颜色』，`sigma_space` 控制空间影响半径——两者一起决定平滑强度。人像磨皮、卡通化预处理的常用一步。

**所属模块**：OpenCV 图像滤波 · **任务类型**：`opencv_bilateral_filter`


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
| `d` | int | `9` | [1, 50] |  |
| `sigma_color` | float | `75` | [0, 300] | 色彩空间滤波强度（值大 = 同等颜色的更大范围被平滑） |
| `sigma_space` | float | `75` | [0, 300] | 坐标空间滤波强度（值大 = 更远像素互相影响） |

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
      "type": "opencv_bilateral_filter",
      "params": {
        "d": 9,
        "sigma_color": 75,
        "sigma_space": 75
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
