---
title: "opencv_threshold · 固定阈值二值化"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "按阈值二值化/截断/归零，含 OTSU 与 TRIANGLE 自动阈值。"
showToc: true
---


固定阈值（`cv::threshold`）。`type` 选择二值/反二值/截断/归零语义；`OTSU_BINARY` / `TRIANGLE_BINARY` 忽略 `thresh` 自动推算全局阈值。

**所属模块**：OpenCV 色彩与二值化 · **任务类型**：`opencv_threshold`


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
| `thresh` | float | `128` | [0, 255] |  |
| `maxval` | float | `255` | [0, 255] |  |
| `type` | enum | `0` | `BINARY` / `BINARY_INV` / `TRUNC` / `TOZERO` / `TOZERO_INV` / `OTSU_BINARY` / `TRIANGLE_BINARY` |  |

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
      "type": "opencv_threshold",
      "params": {
        "thresh": 128,
        "maxval": 255,
        "type": 0
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

## 注意事项

- OTSU / TRIANGLE 模式下 `thresh` 参数被忽略；输入须为 8 位单通道（先转灰度）。

---

> 本文是 [GraphStudio 节点手册](/tags/node-guide/) 系列文章之一；参数与行为以最新发布版为准。对应英文版见页面语言切换。
