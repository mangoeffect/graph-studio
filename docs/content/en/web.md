---
title: Try Online
description: Run GraphStudio (WebAssembly build) directly in your browser — nothing to install.
layout: web
---

GraphStudio is fully compiled to WebAssembly (multi-threaded) and runs directly in the browser.

**The first load is ~16 MB** (wasm module + Qt runtime) — please be patient;
subsequent visits are much faster thanks to the browser cache.

- The page **reloads itself once** after opening — that is expected: the web build
  enables cross-origin isolation via a service worker (SharedArrayBuffer, required
  by multi-threaded wasm); after the reload the page runs in an isolated context.
- In private/incognito windows (service workers disabled) the web build may fail
  to start — use the desktop build instead.
- The OpenCV image subnodes and the JS scripting node are built in, so you can
  assemble and run compute graphs right away; the image-result panel renders
  statically on the web (the desktop build uses a GPU-accelerated viewer).
- For heavy use, [download the desktop installer]({{< relref "download" >}}) for
  better performance and stability.
