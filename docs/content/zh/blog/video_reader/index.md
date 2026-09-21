---
title: "video_reader · 读取视频"
date: 2026-07-12T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "逐帧读取视频文件（mp4 / mov / avi …），每次执行输出一帧。"
showToc: true
---


视频读取节点：executor 按帧驱动，每次 `execute()` 从 `out` 输出下一帧，流结束后收尾。`api_preference` 强制选择解码后端（默认自动，FFMPEG 最常用）。

**所属模块**：视频读写 · **任务类型**：`video_reader`


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
| `file_path` | string | （空） |  | 视频文件路径；相对路径按图目录探测 |
| `api_preference` | enum | `0` | `DEFAULT` / `FFMPEG` | 解码后端：DEFAULT 自动 / FFMPEG |

## 注意事项

- 逐帧语义由图的执行器驱动，GraphStudio 单次执行等于处理一帧；批量转码用 SDK 嵌入式执行或脚本循环。

---

> 本文是 [GraphStudio 节点手册](/tags/node-guide/) 系列文章之一；参数与行为以最新发布版为准。对应英文版见页面语言切换。
