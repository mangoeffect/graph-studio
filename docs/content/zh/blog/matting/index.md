---
title: "matting · 人像抠像"
date: 2026-07-14T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "人像 alpha 抠像：三输出（alpha 结果 / 灰度掩膜 / 透明切图），MediaPipe / MNN 双后端。"
showToc: true
---


估计输入图像的人像 alpha 通道，三个输出端口各有用途：`out` 为结构化结果（含 alpha 与后端信息），`mask` 为 0-255 灰度掩膜，`cutout` 为 RGBA 透明切图（直通 alpha，背景透明）。alpha 经双线性回放对齐原图尺寸。`backend` 语义同 face_detect：MediaPipe（selfie segmenter）优先、降级 MNN（MODNet）。

**所属模块**：人像抠像 · **任务类型**：`matting`


## 端口

**输入**

| 端口 | 数据类型 | 必填 |
|---|---|---|
| `in` | Image / cv::Mat | ✓ |

**输出**

| 端口 | 数据类型 | 必填 |
|---|---|---|
| `out` | Image / cv::Mat | — |
| `mask` | Image / cv::Mat | — |
| `cutout` | Image / cv::Mat | — |

## 参数

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `backend` | enum | `0` | `Auto` / `MediaPipe` / `MNN` | 推理后端选择；Auto 按可用性自动降级（MediaPipe → MNN） |
| `model_path` | string | （空） |  | *随 `backend` 联动显示。* |
| `delegate` | enum | `0` | `CPU` / `GPU` | *随 `backend` 联动显示。*MediaPipe 后端的推理委托（CPU / GPU） |
| `device` | enum | `2` | `CPU` / `Metal` / `Auto` | *随 `backend` 联动显示。*MNN 后端设备（CPU / Metal / Auto） |
| `threads` | int | `4` | [1, 16] | *随 `backend` 联动显示。*CPU 推理线程数 |

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
      "type": "matting",
      "params": {
        "backend": 0,
        "delegate": 0,
        "device": 2,
        "threads": 4
      }
    },
    {
      "id": "save1",
      "type": "opencv_image_write",
      "params": {
        "file_path": "out_out.png"
      }
    },
    {
      "id": "save2",
      "type": "opencv_image_write",
      "params": {
        "file_path": "out_mask.png"
      }
    },
    {
      "id": "save3",
      "type": "opencv_image_write",
      "params": {
        "file_path": "out_cutout.png"
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
      "to": "save1",
      "to_port": "in"
    },
    {
      "from": "node",
      "from_port": "mask",
      "to": "save2",
      "to_port": "in"
    },
    {
      "from": "node",
      "from_port": "cutout",
      "to": "save3",
      "to_port": "in"
    }
  ]
}
```

## 注意事项

- `cutout` 可直接接 `gpu_alpha_composite`（in）与背景图（in2）做最终合成。
- 两后端在标准人像上前景覆盖率交叉一致（±0.1% 量级），可选其一固定使用。
- TG_MATTING_DEBUG=1 打开 MNN 诊断日志。

---

> 本文是 [GraphStudio 节点手册](/tags/node-guide/) 系列文章之一；参数与行为以最新发布版为准。对应英文版见页面语言切换。
