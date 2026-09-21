---
title: "render_gradient · 渐变生成"
date: 2026-07-13T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "无输入生成线性渐变图：测试/背景/合成底图。"
showToc: true
---


渲染一张线性渐变（颜色与方向参数化），零输入节点。常作为 render 链的信号源调试 shader，或做合成底图。

**所属模块**：GPU 渲染 · **任务类型**：`render_gradient`


## 端口

**输入**

*（无）*

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
| `color0` | string | `"0,0,0,1"` |  |  |
| `color1` | string | `"1,1,1,1"` |  |  |

---

> 本文是 [GraphStudio 节点手册](/tags/node-guide/) 系列文章之一；参数与行为以最新发布版为准。对应英文版见页面语言切换。
