---
title: "color_grade_lut · LUT 调色"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "应用 .cube 等 3D LUT 文件做调色（三线性 / 四面体插值）。"
showToc: true
---


读 3D LUT 文件（`.cube`）应用到图像。`interpolation` 选三线性（快）或四面体（更平滑）。摄影调色包通常直接是 .cube 文件，拖进来即用。

**所属模块**：调色（Color Grading） · **任务类型**：`color_grade_lut`


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
| `lut_file` | string | （空） |  | LUT 文件路径（.cube）；相对路径按图目录探测 |
| `interpolation` | enum | `0` | `TRILINEAR` / `TETRAHEDRAL` | 插值方式：TRILINEAR 三线性 / TETRAHEDRAL 四面体 |

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
      "type": "color_grade_lut",
      "params": {
        "interpolation": 0
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

## 注意事项

- GPU 渲染侧等价物是 `render_lut_cube`（.cube → HALD 图）+ `render_lut`（应用），大数据量时更快。

---

> 本文是 [GraphStudio 节点手册](/tags/node-guide/) 系列文章之一；参数与行为以最新发布版为准。对应英文版见页面语言切换。
