---
title: "opencv_gabor_filter · Gabor 滤波"
date: 2026-07-10T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "Gabor 核方向纹理滤波：指纹/织物纹理与方向性特征提取的经典手段。"
showToc: true
---


Gabor 滤波（`cv::getGaborKernel` + filter2D）。按波长、方向、带宽生成正弦调制高斯核，对特定方向与频率的纹理响应最强；多组参数并联可做纹理特征。

**所属模块**：OpenCV 图像滤波 · **任务类型**：`opencv_gabor_filter`


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
| `kernel_size` | int | `31` | [1, 99]（步长 2） | 核边长（像素），须为正奇数 |
| `sigma` | float | `5` | [0, 50] | 高斯包络标准差 |
| `theta` | float | `0` | [0, 6.2832] | 滤波方向角（弧度） |
| `lambd` | float | `10` | [0, 100] | 正弦波长（像素） |
| `gamma` | float | `0.5` | [0, 5] | 空间纵横比 |
| `psi` | float | `1.5708` | [0, 6.2832] | 相位偏移（弧度） |

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
      "type": "opencv_gabor_filter",
      "params": {
        "kernel_size": 31,
        "sigma": 5,
        "theta": 0,
        "lambd": 10,
        "gamma": 0.5,
        "psi": 1.5708
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
