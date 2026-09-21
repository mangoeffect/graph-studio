---
title: "opencv_denoise · 非局部均值去噪"
date: 2026-07-12T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "fastNlMeansDenoising：按图像块相似度去噪，细节保留最好。"
showToc: true
---


非局部均值去噪（`cv::fastNlMeansDenoising`）。以相似块聚合替代局部平滑，高斯噪声去除效果与细节保留都优于双边滤波，代价是速度较慢。`h` 越大去得越狠。

**所属模块**：OpenCV 图像增强 · **任务类型**：`opencv_denoise`


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
| `h` | float | `3` | [0.1, 100] | 滤波强度（亮度）：10 左右起步 |
| `h_color` | float | `3` | [0.1, 100] |  |
| `template_window` | int | `7` | [3, 31] |  |
| `search_window` | int | `21` | [3, 65] |  |

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
      "type": "opencv_denoise",
      "params": {
        "h": 3,
        "h_color": 3,
        "template_window": 7,
        "search_window": 21
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
