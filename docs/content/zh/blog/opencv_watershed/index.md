---
title: "opencv_watershed · 分水岭分割"
date: 2026-07-12T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "经典分水岭：配合标记图（marker）分割粘连目标。"
showToc: true
---


分水岭算法（`cv::watershed`）。需要输入图像 + 标记图（不同整数标记不同种子区域，0 为未知），按梯度地形淹没求得分割边界。上游常用 `opencv_threshold` + `opencv_connected_components` 生成标记。

**所属模块**：OpenCV 图像分割 · **任务类型**：`opencv_watershed`


## 端口

**输入**

| 端口 | 数据类型 | 必填 |
|---|---|---|
| `in` | Image / cv::Mat | ✓ |
| `markers` | Image / cv::Mat | — |

**输出**

| 端口 | 数据类型 | 必填 |
|---|---|---|
| `out` | `cv::Mat` | ✓ |

## 参数

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `rect_x` | int | `-1` | [-1, 16384] |  |
| `rect_y` | int | `-1` | [-1, 16384] |  |
| `rect_width` | int | `-1` | [-1, 16384] |  |
| `rect_height` | int | `-1` | [-1, 16384] |  |

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
      "type": "opencv_watershed",
      "params": {
        "rect_x": -1,
        "rect_y": -1,
        "rect_width": -1,
        "rect_height": -1
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
      "to_port": "markers"
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
