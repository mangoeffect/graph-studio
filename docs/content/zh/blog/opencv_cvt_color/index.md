---
title: "opencv_cvt_color · 色彩空间转换"
date: 2026-07-11T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "BGR/GRAY/RGB/HSV/HLS/LAB/LUV/YCrCb/XYZ 之间的常用转换。"
showToc: true
---


色彩空间转换（`cv::cvtColor`）。按 `code` 枚举选择转换方向。去 `opencv_threshold` 前先转 HSV 可以按亮度/饱和度分割，是调色和掩膜流程的常驻节点。

**所属模块**：OpenCV 色彩与二值化 · **任务类型**：`opencv_cvt_color`


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
| `code` | enum | `6` | 12 项（见表后清单） |  |

**`code` 选项清单**（12）：

- `BGR2GRAY`
- `GRAY2BGR`
- `BGR2RGB`
- `RGB2GRAY`
- `BGR2HSV`
- `HSV2BGR`
- `BGR2HLS`
- `BGR2LAB`
- `BGR2LUV`
- `BGR2YCRCB`
- `YCRCB2BGR`
- `BGR2XYZ`

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
      "type": "opencv_cvt_color",
      "params": {
        "code": 6
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

- 8 位图转 HSV 时 H 范围是 0-179（OpenCV 约定），不是 0-359。

---

> 本文是 [GraphStudio 节点手册](/tags/node-guide/) 系列文章之一；参数与行为以最新发布版为准。对应英文版见页面语言切换。
