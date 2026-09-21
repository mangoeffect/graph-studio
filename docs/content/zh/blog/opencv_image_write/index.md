---
title: "opencv_image_write · 写出图像"
date: 2026-07-10T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "把上游图像写出为文件，格式由扩展名推断（.png / .jpg / .bmp …）。"
showToc: true
---


把输入端口收到的图像写到 `file_path`。输出格式由文件扩展名决定，透明通道会随 PNG 等支持 alpha 的格式保留。

**所属模块**：图像写出 · **任务类型**：`opencv_image_write`


## 端口

**输入**

| 端口 | 数据类型 | 必填 |
|---|---|---|
| `in` | Image / cv::Mat | ✓ |

**输出**

*（无）*

## 参数

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `file_path` | string | （空） |  | 输出图像路径。相对路径按图 JSON 所在目录拼接（不做资产探测，避免覆盖原始资产） |

---

> 本文是 [GraphStudio 节点手册](/tags/node-guide/) 系列文章之一；参数与行为以最新发布版为准。对应英文版见页面语言切换。
