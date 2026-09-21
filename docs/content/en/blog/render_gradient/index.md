---
title: "render_gradient · Gradient"
date: 2026-07-13T10:00:00+08:00
tags: ["node-guide"]
categories: ["Node Guide"]
summary: "Generates a linear gradient with no input: tests, backgrounds, compositing plates."
showToc: true
---


Renders a parameterized linear gradient — a zero-input node. Often the signal source when debugging render-chain shaders, or a clean plate for composites.

**Module**：GPU render · **Task type**：`render_gradient`


## Ports

**Inputs**

*None.*

**Outputs**

| Port | Type | Required |
|---|---|---|
| `out` | `task_graph::Image` | ✓ |

## Parameters

| 参数 | 类型 | 默认值 | 范围 / 选项 | 说明 |
|---|---|---|---|---|
| `script_path` | string | （空） |  | custom render script path (from an effect manifest; empty = built-in effects) |
| `width` | int | `0` | [0, 65535] | target width in pixels; 0 follows the input |
| `height` | int | `0` | [0, 65535] | target height in pixels; 0 follows the input |
| `format` | enum | `0` | `rgba8` / `rgba32f` | render target texture format |
| `clear` | bool | `true` |  | clear the target at pass start |
| `clear_color` | string | `"0,0,0,0"` |  | clear color as comma-separated RGBA components |
| `blend` | bool | `false` |  | enable blended output |
| `color0` | string | `"0,0,0,1"` |  |  |
| `color1` | string | `"1,1,1,1"` |  |  |

---

> Part of the [GraphStudio node guide](/en/tags/node-guide/) series; parameters reflect the latest release.
