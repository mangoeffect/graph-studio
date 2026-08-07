# AGENTS.md

task_graph is a C++20 cross-platform DAG task-execution framework (Desktop / iOS / Android / WASM) with a plugin system, GPU backends, and a Qt6 desktop editor (GraphStudio).

## Build & test

- Tests: `scripts/run_tests.sh` (configure + build + `ctest`). Options: `-c` clean, `-R <regex>` filter, `--opencv` enable OpenCV, `-l` list.
- Focused single test: `cmake --build build --target test_dag -j 8 && cd build && ctest -R test_dag`.
- GraphStudio GUI: `scripts/run_graph_studio.sh` (needs Qt6; `--qt /path/to/qtbase` if CMake can't find it). UI tests: `scripts/run_ui_tests.sh` (headless via `QT_QPA_PLATFORM=offscreen`).
- Submodule scaffolding: `python3 scripts/generate_submodule.py` (CLI or interactive). New subnodes must also be registered in `subnode.json`.
- Build dir is `build/` (gitignored). WASM builds to `build_wasm/`; iOS/Android OpenCV prebuilds live in `build_ios/`/`build_android/`.

## Scripting test history (previously failing, now fixed)

`test_js_engine` and `test_js_mat` (quickjs submodule) used to fail on a clean checkout. Root causes found and fixed (15/15 tests now pass):

- **`test_js_mat` — `JS_SetClassProto` assertion** (`quickjs.c`): `MatWrapper::class_id_` is static, so `registerClass` only called `JS_NewClass` for the *first* `JsRuntime`. Each `JsEngine` owns its own runtime, so the second engine hit the assertion. Fix: gate `JS_NewClass` on `JS_IsRegisteredClass(rt, class_id_)` (not a static guard) so every runtime registers the class once. (`submodules/scripting/js_task/src/js_mat_wrapper.cpp`)
- **`test_js_engine` — error message missing**: `JsEngine::getLastError()` read the `stack` property first; in QuickJS `stack` holds only the backtrace (no message), so the `throw new Error('test error')` message was lost. Fix: read the `message` property first, fall back to `stack`, then `toString`. (`submodules/scripting/js_task/src/js_engine.cpp`)
- **Latent `test_js_mat` stage-3 assertion** (only reachable after the runtime fix): `cv.createMat(width, height, channels)` is width-first (per `brightness.js` usage), so `createMat(100, 200, 3)` → `"100x200x3"`; the old expectation `"200x100x3"` (copied from the rows-first `cv::Mat` ctor) was wrong. (`submodules/scripting/js_task/tests/test_js_mat.cpp`)

## Architecture

- Core library `task_graph` (SHARED on desktop, STATIC on mobile/WASM) in `src/`, headers in `include/task_graph/`.
- Plugin model: `INode` (override `type()/execute()/input_specs()/output_specs()/param_specs()`). Tasks exchange data via ports as `std::any`; `TaskContext::input<T>("port")` reads upstream output. Return via `TaskResult.value` (default port `"out"`) or `outputs`.
- Subnodes in `submodules/` are compile-time-linked plugins driven by `subnode.json` + `cmake/Subnode.cmake`. Each category dir under `submodules/` (opencv/, gpu/, scripting/, mediapipe/) is its own embedded git repo (no remote yet; migrate to GitHub later by adding a remote), and the whole `submodules/` tree is gitignored by the main repo.
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
- The subnode CMakeLists links `libvision.dylib` when present in `build/mediapipe/install/lib`; otherwise it compiles a stub that makes no C API calls. **All four vision tasks are now fully wired** (IMAGE mode): `run_image()` does Image→MpImage conversion then dispatches to a per-task `run_inference(MpImagePtr, VisionResult&, err&)` hook; each task's `on_init()` reads its options from config, calls the task-specific `Mp*Create` (handle reused across `execute()`, closed in the destructor), and `run_inference()` calls `Mp*DetectImage`, maps the C result → `VisionResult`, and frees via `Mp*CloseResult`:
  - **ObjectDetector** → `detections` (bbox normalized [0,1]).
  - **FaceLandmarker** → `face_landmarks` + optional `face_blendshapes` / `face_transformation_matrixes`.
  - **HandLandmarker** → `hand_landmarks` + `hand_world_landmarks` + `handedness`.
  - **PoseLandmarker** → `pose_landmarks` + `pose_world_landmarks` + optional `pose_segmentation_masks` (float32, copied via `MpImageDataFloat32` before `CloseResult`).
  - `running_mode` is currently hardcoded to IMAGE; VIDEO (`DetectForVideo`+`timestamp_ms`) and LIVE_STREAM (`DetectAsync`+callback) are not yet implemented.
- Test models are downloaded (not committed) via `scripts/download_mediapipe_models.sh` into `submodules/mediapipe/mediapipe_vision/tests/models/`; each `test_mediapipe_*` SKIPs if its model is absent. Tests link the plugin directly, build a `TaskContext` with `inputs_by_port["image"]`, and run CPU + GPU (GPU soft-SKIPs if Metal delegate init fails — common on macOS for landmarker models). Note: synthetic test images reliably trigger ObjectDetector and Face; Hand/Pose often yield 0 detections (model-specific), so those tests mainly validate the Create→Detect→Close pipeline, not mapping on real landmarks.
- `libvision.dylib` ships with a **bare install name** (`libvision.dylib`, not `@rpath/...`), so LC_RPATH doesn't apply and `DYLD_LIBRARY_PATH` is stripped under SIP. The tests symlink the prebuilt next to the binaries via a one-shot `mediapipe_vision_libvision` custom target (rm+create_symlink, idempotent) and set `WORKING_DIRECTORY` so dyld's CWD lookup finds it. The real app/plugin loader must do likewise (copy/symlink or fix the install name).
- MediaPipe v1.0.0 has a `rules_java`/Bazel 7.4.1 incompatibility; the working build uses **v0.10.35** (`--version v0.10.35`).