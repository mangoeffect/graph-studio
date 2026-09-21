---
title: "render_lut · GPU LUT 应用"
date: 2026-07-13T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "双输入 LUT 应用：in = 图像、in2 = LUT 图，HALD 与 stripe 布局自动识别。"
showToc: true
---


GPU 上应用 LUT 图（第二输入）：`layout` 自动识别 HALD N²×N² 与 stripe N²×N 两种布局，8 角纹素中心采样手动三线性，`intensity` 控制混合强度。LUT 图由 `render_lut_cube` 从 .cube 文件生成，或直接用任何 HALD 色卡资源。

**所属模块**：GPU 渲染 · **任务类型**：`render_lut`


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
| `script_path` | string | （空） |  | 自定义渲染脚本文件路径（效果清单引用；留空用内置效果） |
| `width` | int | `0` | [0, 65535] | 目标宽度（像素）；0 表示跟随输入尺寸 |
| `height` | int | `0` | [0, 65535] | 目标高度（像素）；0 表示跟随输入尺寸 |
| `format` | enum | `0` | `rgba8` / `rgba32f` | 渲染目标纹理格式 |
| `clear` | bool | `true` |  | pass 开始时是否清屏 |
| `clear_color` | string | `"0,0,0,0"` |  | 清屏颜色 RGBA，逗号分隔四个 0-255/0-1 分量 |
| `blend` | bool | `false` |  | 是否启用混合输出 |
| `intensity` | float | `1` | [0, 1] | LUT 强度；1 = 完全应用，0 = 原图 |
| `layout` | enum | `0` | `auto` / `hald` / `stripe` | LUT 布局：auto 自动识别 / hald / stripe |
| `lut_size` | int | `0` | [0, 256] | LUT 每维级数（0 = 自动） |

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
      "type": "render_lut",
      "params": {
        "width": 0,
        "height": 0,
        "format": 0,
        "clear": true,
        "clear_color": "0,0,0,0",
        "blend": false,
        "intensity": 1,
        "layout": 0,
        "lut_size": 0
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

- `in` 与 `in2` 的角色不能对调；LUT 图建议 `render_lut_cube` 产出。

---

> 本文是 [GraphStudio 节点手册](/tags/node-guide/) 系列文章之一；参数与行为以最新发布版为准。对应英文版见页面语言切换。
