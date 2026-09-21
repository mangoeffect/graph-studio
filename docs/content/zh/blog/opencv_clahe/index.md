---
title: "opencv_clahe · CLAHE 限制对比度均衡"
date: 2026-07-12T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "分块自适应直方图均衡：局部增强且噪声放大可控（clip limit + 网格）。"
showToc: true
---


限制对比度自适应直方图均衡（`cv::createCLAHE`）。按 `tile` 网格分块均衡、`clip_limit` 限制每格直方图高度防止噪声放大；医学/夜景增强的主力。

**所属模块**：OpenCV 图像增强 · **任务类型**：`opencv_clahe`


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
| `clip_limit` | float | `2` | [0.1, 40] | 对比度限制强度（1-4 常用） |
| `tile_x` | int | `8` | [1, 64] |  |
| `tile_y` | int | `8` | [1, 64] |  |

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
      "type": "opencv_clahe",
      "params": {
        "clip_limit": 2,
        "tile_x": 8,
        "tile_y": 8
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
