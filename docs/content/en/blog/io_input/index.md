---
title: "io_input · Graph boundary input"
date: 2026-07-10T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Graph boundary for embedded execution: the host binds a value into io_input before running; downstream tasks consume its out port."
showToc: true
---


Graph-boundary input node for embedded SDK hosts: the host writes a bound image/tensor into this node before execution, and the rest of the graph reads from its `out` port. You normally don't place it manually in GraphStudio — use `opencv_image_read` or another source node instead.

**Module**：Core library · **Task type**：`io_input`


## Ports

**Inputs**

*None.*

**Outputs**

| Port | Type | Required |
|---|---|---|
| `out` | Image / cv::Mat | ✓ |

## Parameters

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `data_type` | string | （空） |  | expected stable type name (e.g. task_graph::Image); empty = no type check |

## Notes

- Registered directly by the core library; no submodule required.

---

> Part of the [GraphStudio node guide](/en/tags/node-guide/) series; parameters reflect the latest release.
