---
title: "video_writer · 写出视频"
date: 2026-07-12T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "把帧序列编码写出为视频文件（fourcc / fps / 是否彩色可配）。"
showToc: true
---


视频写出节点：上游每帧写入，图执行收尾时关闭文件并写 trailer。`fourcc` 用四字符编码名（如 `mp4v`），`fps` 必须与读取端一致。

**所属模块**：视频读写 · **任务类型**：`video_writer`


## 端口

**输入**

| 端口 | 数据类型 | 必填 |
|---|---|---|
| `in` | Image / cv::Mat | ✓ |

**输出**

| 端口 | 数据类型 | 必填 |
|---|---|---|
| `out` | `cv::Mat` | ✓ |

## 参数

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `file_path` | string | （空） |  | 输出视频路径（扩展名决定容器） |
| `fourcc` | string | `"mp4v"` |  | 四字符编码标识，如 mp4v / avc1 |
| `fps` | float | `30` | [1, 240] | 帧率；须与源一致否则快慢放 |
| `is_color` | bool | `true` |  | 是否写彩色帧 |

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
      "type": "video_writer",
      "params": {
        "fourcc": "mp4v",
        "fps": 30,
        "is_color": true
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

---

> 本文是 [GraphStudio 节点手册](/tags/node-guide/) 系列文章之一；参数与行为以最新发布版为准。对应英文版见页面语言切换。
