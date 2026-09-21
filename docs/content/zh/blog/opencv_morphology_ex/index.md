---
title: "opencv_morphology_ex · 形态学组合操作"
date: 2026-07-10T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "开/闭/梯度/顶帽/黑帽等形态学组合：一次节点完成膨胀腐蚀的组合拳。"
showToc: true
---


组合形态学（`cv::morphologyEx`）：开运算去小白点、闭运算填小黑洞、梯度取形态学边缘、顶帽/黑帽提取比邻域亮/暗的局部结构。GPU 渲染侧可用 `render_dilate_dir` + `render_erode_dir` 的 pass 编排表达同类计算。

**所属模块**：OpenCV 图像滤波 · **任务类型**：`opencv_morphology_ex`


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
| `operation` | enum | `2` | `ERODE` / `DILATE` / `OPEN` / `CLOSE` / `GRADIENT` / `TOPHAT` / `BLACKHAT` |  |
| `kernel_size` | int | `3` | [1, 31]（步长 2） | 核边长（像素），须为正奇数 |
| `iterations` | int | `1` | [1, 10] | 重复执行次数 |

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
      "type": "opencv_morphology_ex",
      "params": {
        "operation": 2,
        "kernel_size": 3,
        "iterations": 1
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
