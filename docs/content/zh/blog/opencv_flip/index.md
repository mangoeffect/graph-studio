---
title: "opencv_flip · 翻转"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "水平 / 垂直 / 双向翻转（flip code 0 / 1 / -1）。"
showToc: true
---


镜像翻转（`cv::flip`）。`flip_code` 为 1 水平镜像、0 垂直镜像、-1 双向（旋转 180°）。批量数据增强或摄像头自拍镜像常用。

**所属模块**：OpenCV 几何变换 · **任务类型**：`opencv_flip`


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
| `flip_code` | enum | `1` | `HORIZONTAL` / `VERTICAL` / `BOTH` | 翻转方向：1 = 水平，0 = 垂直，-1 = 双向 |

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
      "type": "opencv_flip",
      "params": {
        "flip_code": 1
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
