---
title: "opencv_connected_components · 连通域标记"
date: 2026-07-12T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "二值图连通域逐个编号，输出标签图（计数见日志）。"
showToc: true
---


连通域分析（`cv::connectedComponents`）。8/4 连通可选，输出每个像素所属域的整数标签；配合统计可做计数、去小面积噪声。

**所属模块**：OpenCV 图像分割 · **任务类型**：`opencv_connected_components`


## 端口

**输入**

| 端口 | 数据类型 | 必填 |
|---|---|---|
| `in` | Image / cv::Mat | ✓ |

**输出**

| 端口 | 数据类型 | 必填 |
|---|---|---|
| `labels` | Image / cv::Mat | — |
| `num_labels` | Image / cv::Mat | — |

## 参数

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `connectivity` | enum | `8` | `C8` / `C4` | 连通性：8 或 4 |

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
      "type": "opencv_connected_components",
      "params": {
        "connectivity": 8
      }
    },
    {
      "id": "save1",
      "type": "opencv_image_write",
      "params": {
        "file_path": "out_labels.png"
      }
    },
    {
      "id": "save2",
      "type": "opencv_image_write",
      "params": {
        "file_path": "out_num_labels.png"
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
      "from_port": "labels",
      "to": "save1",
      "to_port": "in"
    },
    {
      "from": "node",
      "from_port": "num_labels",
      "to": "save2",
      "to_port": "in"
    }
  ]
}
```

---

> 本文是 [GraphStudio 节点手册](/tags/node-guide/) 系列文章之一；参数与行为以最新发布版为准。对应英文版见页面语言切换。
