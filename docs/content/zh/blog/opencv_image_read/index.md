---
title: "opencv_image_read · 读取图像"
date: 2026-07-10T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "从磁盘读取一张图像（png / jpg / bmp / tiff / webp 等 OpenCV 支持的格式），作为图的输入源。"
showToc: true
---


读取一张图像并从 `out` 端口输出，是最常用的图输入源。支持 OpenCV codec 覆盖的全部常见格式。默认输出 BGR 三通道；开启 `keep_alpha` 后保留 alpha 通道（四通道 RGBA），供混合、抠像等需要透明度的下游任务使用。

**所属模块**：图像读取 · **任务类型**：`opencv_image_read`


## 端口

**输入**

*（无）*

**输出**

| 端口 | 数据类型 | 必填 |
|---|---|---|
| `out` | `cv::Mat` | ✓ |

## 参数

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `file_path` | string | （空） |  | 图像文件路径。相对路径按图 JSON 所在目录探测（图目录及其上级），绝对路径原样使用 |
| `keep_alpha` | bool | `false` |  |  |

## 注意事项

- 路径缺失或解码失败时任务返回 FAILED，日志面板会给出可读原因。

---

> 本文是 [GraphStudio 节点手册](/tags/node-guide/) 系列文章之一；参数与行为以最新发布版为准。对应英文版见页面语言切换。
