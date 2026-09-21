---
title: "opencv_resize · 缩放"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "按目标宽高与插值方法缩放图像。"
showToc: true
---


图像缩放（`cv::resize`）。缩小用 `INTER_AREA` 抗摩尔纹、放大用 `INTER_LINEAR`/`INTER_CUBIC` 平滑；近邻 `INTER_NEAREST` 保像素值（掩码/标签图必用）。

**所属模块**：OpenCV 几何变换 · **任务类型**：`opencv_resize`


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
| `width` | int | `0` | [0, 8192] | 目标宽度（像素）；0 表示跟随输入尺寸 |
| `height` | int | `0` | [0, 8192] | 目标高度（像素）；0 表示跟随输入尺寸 |
| `scale_x` | float | `1` | [0.01, 32] |  |
| `scale_y` | float | `1` | [0.01, 32] |  |
| `interpolation` | enum | `1` | `LINEAR` / `NEAREST` / `CUBIC` / `LANCZOS4` / `AREA` | 插值方法 |

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
      "type": "opencv_resize",
      "params": {
        "width": 0,
        "height": 0,
        "scale_x": 1,
        "scale_y": 1,
        "interpolation": 1
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
