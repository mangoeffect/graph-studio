---
title: "opencv_apply_color_map · 伪彩色映射"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "把单通道图按预置色表上色（JET / VIRIDIS / BONE …），热力图可视化一步到位。"
showToc: true
---


伪彩色映射（`cv::applyColorMap`）。灰度/深度/置信度图按 `colormap` 枚举染色，输出三通道可视化图。

**所属模块**：OpenCV 色彩与二值化 · **任务类型**：`opencv_apply_color_map`


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
| `colormap` | enum | `2` | 17 项（见表后清单） |  |

**`colormap` 选项清单**（17）：

- `AUTUMN`
- `BONE`
- `JET`
- `WINTER`
- `RAINBOW`
- `OCEAN`
- `HOT`
- `HSV`
- `PINK`
- `PARULA`
- `MAGMA`
- `INFERNO`
- `PLASMA`
- `VIRIDIS`
- `CIVIDIS`
- `TWILIGHT`
- `TURBO`

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
      "type": "opencv_apply_color_map",
      "params": {
        "colormap": 2
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
