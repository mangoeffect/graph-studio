---
title: "color_grade_curves · 曲线调色"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "主曲线与 R/G/B 通道曲线：控制点字符串描述的样条调色。"
showToc: true
---


Photoshop 曲线式调色。每条曲线用控制点序列（x,y 对，逗号分隔）描述，按单调样条插值成 256 级查找表后应用。主曲线调明度对比，RGB 曲线调色偏。

**所属模块**：调色（Color Grading） · **任务类型**：`color_grade_curves`


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
| `curve_y` | string | `"0,0;1,1"` |  |  |
| `curve_r` | string | `"0,0;1,1"` |  |  |
| `curve_g` | string | `"0,0;1,1"` |  |  |
| `curve_b` | string | `"0,0;1,1"` |  |  |
| `interp` | enum | `0` | `LINEAR` / `CUBIC` |  |

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
      "type": "color_grade_curves",
      "params": {
        "curve_y": "0,0;1,1",
        "curve_r": "0,0;1,1",
        "curve_g": "0,0;1,1",
        "curve_b": "0,0;1,1",
        "interp": 0
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
