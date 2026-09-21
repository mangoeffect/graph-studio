---
title: "color_grade_mixer · 通道混合器"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "RGB 输出通道按输入通道加权重组，做色调分离/通道对调。"
showToc: true
---


通道混合器：每个输出通道 = R/G/B 输入的加权和（3×3 权重字符串）。经典玩法包括红青对调、黑白转换权重微调、伪红外色调。

**所属模块**：调色（Color Grading） · **任务类型**：`color_grade_mixer`


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
| `m_rr` | float | `1` | [-2, 2] |  |
| `m_rg` | float | `0` | [-2, 2] |  |
| `m_rb` | float | `0` | [-2, 2] |  |
| `m_gr` | float | `0` | [-2, 2] |  |
| `m_gg` | float | `1` | [-2, 2] |  |
| `m_gb` | float | `0` | [-2, 2] |  |
| `m_br` | float | `0` | [-2, 2] |  |
| `m_bg` | float | `0` | [-2, 2] |  |
| `m_bb` | float | `1` | [-2, 2] |  |

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
      "type": "color_grade_mixer",
      "params": {
        "m_rr": 1,
        "m_rg": 0,
        "m_rb": 0,
        "m_gr": 0,
        "m_gg": 1,
        "m_gb": 0,
        "m_br": 0,
        "m_bg": 0,
        "m_bb": 1
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
