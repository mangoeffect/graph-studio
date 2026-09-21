---
title: "render_pipeline · 多 pass 渲染管线"
date: 2026-07-13T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "任务级多 pass 编排：passes 数量 + pass{i}_* 扁平参数键表达整条滤镜链。"
showToc: true
---


在一个任务里串起 N 个渲染 pass：`passes` 声明数量，每个 pass 的效果与参数用 `pass1_effect`、`pass1_intensity` … 的扁平键描述（TaskParams 是扁平 map，嵌套结构装不进去）。复合滤镜（二维高斯 = 横向+纵向、形态学开闭 = 腐蚀+膨胀）都用它表达，不再新增复合任务类型。

**所属模块**：GPU 渲染 · **任务类型**：`render_pipeline`


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
| `passes` | int | `2` | [1, 64] | pass 数量（1-64） |
| `width` | int | `0` | [0, 65535] | 输出画布宽；0 跟随输入 |
| `height` | int | `0` | [0, 65535] | 输出画布高；0 跟随输入 |
| `effects_path` | string | （空） |  | 效果清单（目录）路径；内置效果留空 |

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
      "type": "render_pipeline",
      "params": {
        "passes": 2,
        "width": 0,
        "height": 0
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

- pass{i}_effect 指定每段效果名，其余 pass{i}_<参数> 透传给该效果。
- 示例与可运行的范本图见仓库 `submodules/render/render_task/tests/graphs/`。

---

> 本文是 [GraphStudio 节点手册](/tags/node-guide/) 系列文章之一；参数与行为以最新发布版为准。对应英文版见页面语言切换。
