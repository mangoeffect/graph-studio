---
title: "opencv_gamma_correct · 伽马校正"
date: 2026-07-12T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "按幂律（out = in^gamma）调整明暗，显示器匹配与提亮暗部。"
showToc: true
---


伽马校正：查表实现幂律变换。`gamma` < 1 提亮暗部、> 1 压暗；线性→sRGB 或逆向转换也是同一节点。

**所属模块**：OpenCV 图像增强 · **任务类型**：`opencv_gamma_correct`


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
| `gamma` | float | `1` | [0.01, 10] | 幂指数；0.45 附近为线性→sRGB |

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
      "type": "opencv_gamma_correct",
      "params": {
        "gamma": 1
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
