---
title: "opencv_adaptive_threshold · 自适应阈值"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "按邻域局部统计（均值/高斯）二值化，抗光照不均。"
showToc: true
---


自适应阈值（`cv::adaptiveThreshold`）。阈值逐像素由邻域均值/高斯加权减去常数 C 得到，适合光照渐变的文档扫描、阴影场景。

**所属模块**：OpenCV 色彩与二值化 · **任务类型**：`opencv_adaptive_threshold`


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
| `max_value` | float | `255` | [0, 255] |  |
| `block_size` | int | `11` | [3, 99] | 邻域边长（正奇数） |
| `C` | float | `2` | [-50, 50] |  |
| `adaptive_method` | enum | `0` | `MEAN_C` / `GAUSSIAN_C` |  |
| `threshold_type` | enum | `0` | `BINARY` / `BINARY_INV` |  |

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
      "type": "opencv_adaptive_threshold",
      "params": {
        "max_value": 255,
        "block_size": 11,
        "C": 2,
        "adaptive_method": 0,
        "threshold_type": 0
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
