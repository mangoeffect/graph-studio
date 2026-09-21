---
title: "opencv_grabcut · GrabCut 前背景分割"
date: 2026-07-12T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "以矩形或掩膜初始化的迭代图割分割：交互式抠图的经典算法。"
showToc: true
---


GrabCut（`cv::grabCut`）。`init_mode` 选矩形（给定包含目标的框）或掩膜（给定粗略前后景标记），迭代图割能量最小化输出精细前景掩膜。人像/物体抠图的 OpenCV 侧入口；高质量人像请用 `matting` 节点。

**所属模块**：OpenCV 图像分割 · **任务类型**：`opencv_grabcut`


## 端口

**输入**

| 端口 | 数据类型 | 必填 |
|---|---|---|
| `in` | Image / cv::Mat | ✓ |
| `mask` | Image / cv::Mat | — |

**输出**

| 端口 | 数据类型 | 必填 |
|---|---|---|
| `out` | `cv::Mat` | ✓ |

## 参数

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `init_mode` | enum | `0` | `RECT` / `MASK` |  |
| `rect_x` | int | `0` | [0, 16384] |  |
| `rect_y` | int | `0` | [0, 16384] |  |
| `rect_width` | int | `0` | [0, 16384] |  |
| `rect_height` | int | `0` | [0, 16384] |  |
| `iterations` | int | `5` | [1, 100] | 重复执行次数 |

## 示例图

示例图为最小可运行流水线（读取 → 处理 → 写出），可直接拖入 GraphStudio 或保存为 `.json` 后用 `--open` 打开；`data/test.png` 替换为实际图像路径。

```json
{
  "version": "2.0",
  "tasks": [
    {
      "id": "read1",
      "type": "opencv_image_read",
      "params": {
        "file_path": "data/test.png"
      }
    },
    {
      "id": "read2",
      "type": "opencv_image_read",
      "params": {
        "file_path": "data/test.png"
      }
    },
    {
      "id": "node",
      "type": "opencv_grabcut",
      "params": {
        "init_mode": 0,
        "rect_x": 0,
        "rect_y": 0,
        "rect_width": 0,
        "rect_height": 0,
        "iterations": 5
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
      "from": "read1",
      "from_port": "out",
      "to": "node",
      "to_port": "in"
    },
    {
      "from": "read2",
      "from_port": "out",
      "to": "node",
      "to_port": "mask"
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

- RECT 模式下 rect_x/y/width/height 定义目标框；MASK 模式忽略矩形参数。
- 迭代越多边缘越精细也越慢，5-10 次通常够用。

---

> 本文是 [GraphStudio 节点手册](/tags/node-guide/) 系列文章之一；参数与行为以最新发布版为准。对应英文版见页面语言切换。
