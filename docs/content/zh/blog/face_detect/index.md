---
title: "face_detect · 人脸检测"
date: 2026-07-14T10:00:00+08:00
tags: ["node-guide"]
categories: ["节点手册"]
summary: "人脸框检测（可开 478 点关键点）：MediaPipe / MNN 双后端，auto 自动降级。"
showToc: true
---


检测输入图像中的人脸，输出人脸框；`output_landmarks` 开启后叠加 478 点关键点（mediapipe_478 方案，两后端统一）。`backend` 为 auto 时 MediaPipe 优先、不可用自动降级 MNN；实际使用的后端写在结果里可观测。

**所属模块**：人脸检测 · **任务类型**：`face_detect`


## 端口

**输入**

| 端口 | 数据类型 | 必填 |
|---|---|---|
| `in` | Image / cv::Mat | ✓ |

**输出**

| 端口 | 数据类型 | 必填 |
|---|---|---|
| `out` | Image / cv::Mat | — |

## 参数

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `backend` | enum | `0` | `Auto` / `MediaPipe` / `MNN` | 推理后端选择；Auto 按可用性自动降级（MediaPipe → MNN） |
| `model_path` | string | （空） |  | *随 `backend` 联动显示。*MNN 后端检测模型路径（空 = 内置查找路径） |
| `landmark_model_path` | string | （空） |  | *随 `backend` 联动显示。*关键点模型路径（output_landmarks 时使用） |
| `output_landmarks` | bool | `false` |  | 是否输出 478 点关键点 |
| `max_faces` | int | `5` | [1, 10] | 最多保留的人脸数 |
| `score_threshold` | float | `0.5` | [0, 1] | 置信度阈值 |
| `nms_threshold` | float | `0.3` | [0, 1] | NMS 重叠阈值 |
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
      "type": "face_detect",
      "params": {
        "backend": 0,
        "output_landmarks": false,
        "max_faces": 5,
        "score_threshold": 0.5,
        "nms_threshold": 0.3,
        "delegate": 0,
        "device": 2,
        "threads": 4
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

## 注意事项

- 模型资产缺失时按后端语义返回 FAILED（提示跑下载脚本），不会静默空结果。
- TG_FACE_DEBUG=1 环境变量打开后端诊断日志。

---

> 本文是 [GraphStudio 节点手册](/tags/node-guide/) 系列文章之一；参数与行为以最新发布版为准。对应英文版见页面语言切换。
