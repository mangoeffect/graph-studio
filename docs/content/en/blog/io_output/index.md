---
title: "io_output · Graph boundary output"
date: 2026-07-10T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Graph boundary for embedded execution: the host collects the graph result from io_output after the run."
showToc: true
---


Graph-boundary output node: after execution the host collects the result from it (the upstream port value passes through unchanged). Not needed on the GraphStudio canvas — use the image viewer or `opencv_image_write` to inspect results.

**Module**：Core library · **Task type**：`io_output`


## Ports

**Inputs**

| Port | Type | Required |
|---|---|---|
| `in` | Image / cv::Mat | ✓ |

**Outputs**

*None.*

## Parameters

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|

## Notes

- Registered directly by the core library; no submodule required.

---

> Part of the [GraphStudio node guide](/en/tags/node-guide/) series; parameters reflect the latest release.
