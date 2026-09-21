---
title: "blend · 图层混合（27 种模式）"
date: 2026-07-14T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "Photoshop 27 种图层混合模式的双输入合成：GPU 优先执行、CPU 镜像兜底。"
showToc: true
---


把 `in`（上层）按 `mode` 混合到 `in2`（下层），实现 Photoshop 全套 27 种混合模式（正常/溶解/变暗族/变亮族/叠加族/差值族/HSL 族）。`opacity` 控制整体不透明度。`device` 选 auto 时 GPU compute 优先、失败自动落 CPU 镜像实现——结果一致、环境不挑。溶解类模式的噪声由 `seed` 决定，可复现。

**所属模块**：图层混合 · **任务类型**：`blend`


## 端口

**输入**

| 端口 | 数据类型 | 必填 |
|---|---|---|
| `in` | Image / cv::Mat | ✓ |
| `in2` | Image / cv::Mat | ✓ |

**输出**

| 端口 | 数据类型 | 必填 |
|---|---|---|
| `out` | `task_graph::Image` | ✓ |

## 参数

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `mode` | enum | `0` | 27 项（见表后清单） | 混合模式（27 项清单见表后） |
| `opacity` | float | `1` | [0, 1] | 混合结果的不透明度 |
| `device` | enum | `0` | `auto` / `gpu` / `cpu` | 执行设备：auto = GPU 优先、失败回退 CPU 镜像实现 |
| `seed` | int | `42` | [0, 2147480000] | 溶解（dissolve）类模式的随机种子 |

**`mode` 选项清单**（27）：

- `normal`
- `dissolve`
- `darken`
- `multiply`
- `color_burn`
- `linear_burn`
- `darker_color`
- `lighten`
- `screen`
- `color_dodge`
- `linear_dodge`
- `lighter_color`
- `overlay`
- `soft_light`
- `hard_light`
- `vivid_light`
- `linear_light`
- `pin_light`
- `hard_mix`
- `difference`
- `exclusion`
- `subtract`
- `divide`
- `hue`
- `saturation`
- `color`
- `luminosity`

## 示例图

示例图为最小可运行流水线（读取 → 处理 → 写出），可直接拖入 GraphStudio 或保存为 `.json` 后用 `--open` 打开；`data/test.png` 替换为实际图像路径。

```json
{
  "version": "2.0",
  "tasks": [
    {
      "id": "read1",
      "type": "opencv_image_read",
      "params": {
        "file_path": "data/test.png"
      }
    },
    {
      "id": "read2",
      "type": "opencv_image_read",
      "params": {
        "file_path": "data/test.png"
      }
    },
    {
      "id": "node",
      "type": "blend",
      "params": {
        "mode": 0,
        "opacity": 1,
        "device": 0,
        "seed": 42
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
      "from": "read1",
      "from_port": "out",
      "to": "node",
      "to_port": "in"
    },
    {
      "from": "read2",
      "from_port": "out",
      "to": "node",
      "to_port": "in2"
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

- 两路输入尺寸须一致；通道数不一致时按混合模式语义处理 alpha。
- HSL 族（hue/saturation/color/luminosity）按 Photoshop 传递函数实现。

---

> 本文是 [GraphStudio 节点手册](/tags/node-guide/) 系列文章之一；参数与行为以最新发布版为准。对应英文版见页面语言切换。
