---
title: "gpu_box_blur · GPU 均值模糊"
date: 2026-07-13T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "GPU compute 管线的方框均值模糊，与 opencv_blur_filter 语义对齐。"
showToc: true
---


在 GPU compute 后端（wgpu/Metal/Vulkan）上执行的均值模糊，CPU 参考实现逐位对齐。大图批量处理时比 CPU 版快一个量级，参数语义与 `opencv_blur_filter` 一致。

**所属模块**：GPU 图像处理 · **任务类型**：`gpu_box_blur`


## 端口

**输入**

| 端口 | 数据类型 | 必填 |
|---|---|---|
| `in` | Image / cv::Mat | ✓ |

**输出**

| 端口 | 数据类型 | 必填 |
|---|---|---|
| `out` | `task_graph::Image` | ✓ |

## 参数

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `kernel_size` | int | `5` | [1, 99]（步长 2） | 核边长（像素），须为正奇数 |

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
      "type": "gpu_box_blur",
      "params": {
        "kernel_size": 5
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

- GPU 后端初始化失败时任务失败（不会静默回退 CPU；需要回退语义请用 blend 的 device=auto）。

---

> 本文是 [GraphStudio 节点手册](/tags/node-guide/) 系列文章之一；参数与行为以最新发布版为准。对应英文版见页面语言切换。
