---
title: "io_input · 图边界输入"
date: 2026-07-10T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "SDK 嵌入式执行的图边界：宿主在执行入口把绑定值写入 io_input，图内任务从其 out 端口消费。"
showToc: true
---


图边界输入节点，供嵌入式宿主（SDK）注入数据：执行前宿主把绑定的图像/张量写入本节点，图内其余任务从它的 `out` 端口读取。在 GraphStudio 画布上一般不需要手动创建——编辑器场景请使用 `opencv_image_read` 等源节点。

**所属模块**：核心库 · **任务类型**：`io_input`


## 端口

**输入**

*（无）*

**输出**

| 端口 | 数据类型 | 必填 |
|---|---|---|
| `out` | Image / cv::Mat | ✓ |

## 参数

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `data_type` | string | （空） |  | expected stable type name (e.g. task_graph::Image); empty = no type check |

## 注意事项

- 任务类型由核心库直接注册，不依赖任何子模块。

---

> 本文是 [GraphStudio 节点手册](/tags/node-guide/) 系列文章之一；参数与行为以最新发布版为准。对应英文版见页面语言切换。
