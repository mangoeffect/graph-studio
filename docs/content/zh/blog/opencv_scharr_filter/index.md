---
title: "opencv_scharr_filter · Scharr 导数"
date: 2026-07-10T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "Scharr 算子：3× 核下比 Sobel 更精确的旋转对称导数，小核边缘检测首选。"
showToc: true
---


Scharr 导数（`cv::Scharr`）。仅 3×3 核，但角度误差小于同尺寸 Sobel；需要更高精度梯度或 HK 轮廓提取时用它替换 Sobel。

**所属模块**：OpenCV 图像滤波 · **任务类型**：`opencv_scharr_filter`


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
| `dx` | int | `1` | [0, 1] | x 方向求导阶数 |
| `dy` | int | `1` | [0, 1] | y 方向求导阶数 |

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
      "type": "opencv_scharr_filter",
      "params": {
        "dx": 1,
        "dy": 1
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
