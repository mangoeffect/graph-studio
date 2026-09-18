---
title: "v0.1.0-alpha.13: the web edition is fully usable, plus vision tasks and project bundles"
date: 2026-09-17T20:00:00+08:00
tags: ["Release", "GraphStudio", "WASM"]
categories: ["Release Notes"]
summary: "This alpha turns GraphStudio-in-the-browser from 'it opens' into 'it works': drag & drop, context menus, and whole-folder drops all function; face detection and portrait matting ship with default models; plus .tgp single-file project bundles and wgpu as the default GPU backend."
showToc: true
---

A month of work after [v0.1.0-alpha.8](https://github.com/mangoeffect/graph-studio/releases/tag/v0.1.0-alpha.8). The headline of this release: the browser build becomes genuinely usable, vision tasks arrive, and graphs become shareable as single files.

## Getting it

- **Try it online (no install)**: [GraphStudio on the web](https://studio.mangoeffect.net/web/) — drop in a `graph.json`, an entire `graphs/` folder, or a `.tgp` project bundle;
- **Desktop installers**: [download page]({{< relref "download" >}}) (macOS `.dmg` / Windows `.msix` / Linux `.AppImage`).

## The WASM web build, fully usable

The previous web build opened but fought you — no context menus, no palette drag, and dropped-in graphs couldn't find their assets. All fixed in this release:

- **Context menus**: `QMenu::exec()` doesn't work on Qt 6.6 WASM; replaced with a non-blocking `popup()`, one code path for desktop and web (canvas nodes also gained right-click delete);
- **Palette drag & drop**: rebuilt on an application-level event filter, sidestepping the platform's broken `grabMouse`;
- **File & folder drops**: a loose `graph.json` gets its assets paired automatically by in-graph references; dropping a whole folder (e.g. a submodule's `tests/graphs/`) lands with the original relative layout so sibling `data/` assets resolve; missing assets are reported in the in-app log panel instead of failing silently;
- **URL opening**: `?open=<url-to-graph.json>&run=1` cold-starts and runs a graph — handy for sharing one-click runnable graphs;
- **Models bundled**: the default face/matting models ship inside the web package, so vision tasks work in the browser out of the box.

## New tasks: face detection and portrait matting

- **`face_detect`** — face boxes by default, 478-point landmarks with `output_landmarks=true`; mediapipe / mnn dual backends with automatic fallback in `auto` mode;
- **`matting`** — portrait matting with three outputs: `out` (alpha mask), `mask` (grayscale), and `cutout` (transparent-background result); dual backends as well.

Both live in the new **Vision** palette category. Model parameters show/hide per backend, and default models are bundled with both the desktop installers and the web package — zero configuration.

## .tgp project bundles: share a whole project as one file

A browser can't reach sibling directories on your disk, so sending someone a lone `graph.json` always meant missing files. The new **project bundle (`.tgp`)** packs the graph JSON plus every relative-path dependency into a single ZIP: it opens from the toolbar, drag & drop, and the URL channel on both desktop and web. Saving from an opened project exports a fresh bundle, so nothing is lost.

## wgpu is now the default GPU backend

The GPU compute path moves to **wgpu**: WGSL single-source (one shader set across Metal / Vulkan / D3D12), all 17 compute ops ported and running on wgpu by default; the hand-written Metal / Vulkan backends remain as fallbacks and baselines (force one via `TG_GPU_BACKEND`). The render side gains WGSL sources too, along with a set of filter primitives: `render_lut` (HALD/stripe layouts auto-detected), `render_lut_cube` (`.cube` conversion), separable Gaussian/box blur, Sobel/Scharr/Laplacian, dilate/erode, unsharp mask, and more — compose complex filters with `render_pipeline`'s `pass{i}_*` parameters.

## Engines and scripting move into the core library

The MNN inference engine, the ten MediaPipe vision tasks (`mp_*`), and the QuickJS engine (the `js_script` task) are no longer distributed as separate plugin repositories — they compile straight into the core library, simplifying packages and removing plugin prerequisites. Render effects additionally support JS lifecycle scripts (`onInit` / `onParamChange` / `onBeforeRender` / `onAfterRender` / `onRelease`).

## Other improvements

- **Image viewer rewritten**: pure QPainter (no native-window/GL dependency), one code path on desktop and web; GPU-resident results sync back to the CPU lazily, on selection, avoiding full readbacks of large images;
- **Asset path resolution**: read-type asset references probe the graph's directory plus two ancestors, so source-tree test graphs now run when dropped straight into GraphStudio;
- **Stability**: fixed the Windows MSVC Debug link error (LNK2019) and the exit-time segfault in GPU graph tests on Linux;
- **For developers**: a pure C API (`tg_sdk_c.h`), the TaskGraphSdk lifecycle API, the read-only DagConfig API, a unified ModelFinder, and a five-track automated test suite covering desktop / WASM / macOS / Android.

## Full changelog

See the [GitHub Release v0.1.0-alpha.13](https://github.com/mangoeffect/graph-studio/releases/tag/v0.1.0-alpha.13).

---

Try it live at [studio.mangoeffect.net/web](https://studio.mangoeffect.net/web/). Issues are welcome on [GitHub](https://github.com/mangoeffect/graph-studio/issues).
