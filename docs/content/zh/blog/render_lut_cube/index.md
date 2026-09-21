---
title: "render_lut_cube · .cube 转 LUT 图"
date: 2026-07-13T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "读取 .cube 3D LUT 文件，CPU 转换为 HALD LUT 图输出（接 render_lut 的 in2）。"
showToc: true
---


把 `.cube` 文本 LUT 烘焙成 HALD LUT 图（DOMAIN 归一化在转换时完成），输出接 `render_lut` 的 `in2` 完成 GPU 调色。与 `color_grade_lut`（CPU 直接应用）是同一素材的两条路径。

**所属模块**：GPU 渲染 · **任务类型**：`render_lut_cube`


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
| `lut_file` | string | （空） |  |  |

---

> 本文是 [GraphStudio 节点手册](/tags/node-guide/) 系列文章之一；参数与行为以最新发布版为准。对应英文版见页面语言切换。
