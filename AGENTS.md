# AGENTS.md

task_graph is a C++20 cross-platform DAG task-execution framework (Desktop / iOS / Android / WASM) with a plugin system, GPU backends, and a Qt6 desktop editor (GraphStudio).

## Build & test

- Tests: `scripts/run_tests.sh` (configure + build + `ctest`). Options: `-c` clean, `-R <regex>` filter, `--opencv` enable OpenCV, `-l` list.
- Focused single test: `cmake --build build --target test_dag -j 8 && cd build && ctest -R test_dag`.
- GraphStudio GUI: `scripts/run_graph_studio.sh` (needs Qt6; `--qt /path/to/qtbase` if CMake can't find it). UI tests: `scripts/run_ui_tests.sh` (headless via `QT_QPA_PLATFORM=offscreen`).
- Submodule scaffolding: `python3 scripts/generate_submodule.py` (CLI or interactive). New subnodes must also be registered in `subnode.json`.
- Build dir is `build/` (gitignored). WASM builds to `build_wasm/`; iOS/Android OpenCV prebuilds live in `build_ios/`/`build_android/`.

## Known test failures (pre-existing, NOT caused by your changes)

`test_js_engine` and `test_js_mat` (quickjs submodule) crash with `JS_SetClassProto` assertion in `quickjs.c`. They fail on a clean checkout too — do not chase them.

## Architecture

- Core library `task_graph` (SHARED on desktop, STATIC on mobile/WASM) in `src/`, headers in `include/task_graph/`.
- Plugin model: `INode` (override `type()/execute()/input_specs()/output_specs()/param_specs()`). Tasks exchange data via ports as `std::any`; `TaskContext::input<T>("port")` reads upstream output. Return via `TaskResult.value` (default port `"out"`) or `outputs`.
- Subnodes in `submodules/` are compile-time-linked plugins driven by `subnode.json` + `cmake/Subnode.cmake`. `submodules/` is gitignored except `submodules/mediapipe/` (via `.gitignore` negation).
- Custom domain types used across ports must be registered with `TG_REGISTER_TYPE(Type, "stable::name")` for stable cross-SO type names.
- GPU backends: Metal (Apple), Vulkan, CUDA — all opt-in CMake flags (`TASK_GRAPH_ENABLE_METAL/VULKAN/CUDA`), all OFF by default. OpenCV is also opt-in (`TASK_GRAPH_ENABLE_OPENCV`).

## Conventions

- Follow the pattern in `submodules/opencv/image_processing/image_filtering/`: type-name constants as `const char* const kXxxType` (avoid static-init order bugs), `type()` returns a `static const std::string`, and register both via `__attribute__((constructor))` AND `extern "C" register_plugin/unregister_plugin`.
- WASM/mobile are built with `-fno-exceptions`; do not throw across plugin boundaries — return `TaskResult{FAILED}` instead.
- Submodule CMakeLists use `TASK_GRAPH_ROOT` so they work both via `add_subdirectory` and standalone.
- Commit messages use conventional commits (e.g. `fix(studio): ...`, `feat(mediapipe): ...`).

## MediaPipe subnode (mediapipe_vision)

- `scripts/build_mediapipe_macos.sh` builds `libvision.dylib` (full MediaPipe framework, C API only exported) into `build/mediapipe/install/`. Requires `brew install bazelisk`; Docker on macOS only produces Linux binaries and cannot link into the macOS build.
- The script patches several upstream incompatibilities: zlib `TARGET_OS_MAC` fdopen (Xcode 26), eigen gitlab 403 (local override), OpenCV5 API (`getPerspectiveTransform` moved to `geometry/2d.hpp`, `boxPoints` removed, `calib3d`→`calib`/`features2d`→`features`), and protobuf symbol conflicts (exported_symbols_list restricts to ~48 `Mp*` C API symbols).
- The subnode CMakeLists links `libvision.dylib` when present in `build/mediapipe/install/lib`; otherwise it compiles a stub that makes no C API calls. `run_image()` in `mediapipe_vision.cpp` still has a `// TODO: invoke FaceLandmarkerCreate/DetectImage/Close` — the landmarker calls are not yet wired; it currently only does Image→MpImage conversion.
- MediaPipe v1.0.0 has a `rules_java`/Bazel 7.4.1 incompatibility; the working build uses **v0.10.35** (`--version v0.10.35`).