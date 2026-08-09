# AGENTS.md

task_graph is a C++20 cross-platform DAG task-execution framework (Desktop / iOS / Android / WASM) with a plugin system, GPU backends, and a Qt6 desktop editor (GraphStudio).

## Build & test

- Tests: `scripts/run_tests.sh` (configure + build + `ctest`). Options: `-c` clean, `-R <regex>` filter, `--opencv` enable OpenCV, `-l` list.
- Focused single test: `cmake --build build --target test_dag -j 8 && cd build && ctest -R test_dag`.
- GraphStudio GUI: `scripts/run_graph_studio.sh` (needs Qt6; `--qt /path/to/qtbase` if CMake can't find it). UI tests: `scripts/run_ui_tests.sh` (headless via `QT_QPA_PLATFORM=offscreen`).
- Submodule scaffolding: `python3 scripts/generate_submodule.py` (CLI or interactive). New subnodes must also be registered in `subnode.json`.
- Build dir is `build/` (gitignored). WASM builds to `build_wasm/`; iOS/Android OpenCV prebuilds live in `build_ios/`/`build_android/`.

## Submodule graph-driven tests (uniform pattern)

All subnode tests are JSON-graph-driven: each test executable loads a `<name>_graph.json` (via `DAGSerializer::from_string`, executed by `DAGExecutor`) describing `opencv_image_read` (committed `tests/data/test.png`) → task(s). This covers `mediapipe_vision`, plus the `opencv` (`image_filtering`, `image_reader`), `gpu`, and `scripting` submodules.

- **Layout** (all inside each submodule dir): `tests/graphs/*.json.in` templates, `tests/data/test.png` (deterministic 128×128 synthetic), test binary `tests/test_<name>_graph.cpp`, and the CMake `add_test` registration lives in the submodule's own `CMakeLists.txt`.
- **Path mechanism**: `configure_file` bakes absolute paths at configure time — graphs use `@DATA_DIR@`/`@SCRIPTS_DIR@` placeholders; generated `.json` goes to `<binary_dir>/graphs/`. Committed assets use `configure_file` (@ONLY) so no post-configure script is needed. The mediapipe tests use the same mechanism with an `@MODELS_DIR@` placeholder into the gitignored downloaded models dir (templates are still committed as `.json.in`).
- **`configure_file` placeholder gotcha**: the CMake variable name must match the placeholder exactly (e.g. `DATA_DIR`, not `TEST_DATA_DIR`), or the value substitutes empty.
- **Ordering in `subnode.json`**: the compile-time-linked subnode that provides a task type must be listed *before* any subnode whose tests link it (CMake `target_link_libraries` needs the target to exist at configure time). `image_reader` is listed first because all graph tests use `opencv_image_read` as the source.
- **Test content and SKIPs**: `gpu` tests set up a Metal backend and soft-SKIP if it fails to init; asserts pixel-exact box_blur vs a CPU reference. `scripting` tests use `tests/scripts/*.js` (engine_arith/mat_ops/error_throw) and assert output values + the JS-thrown-error path fails the task with a readable message.
- The mediapipe tests soft-SKIP when the downloaded model/image assets are absent (fresh checkout, before `scripts/download_mediapipe_models.sh`); the graphs themselves are always materialized by `configure_file`.

## Scripting test history (previously failing, now fixed)

The old `test_js_engine`/`test_js_mat` binaries were replaced by the JSON-graph-driven `test_js_script_graph` (see below). The historical root causes and their source fixes remain relevant:

- **`JS_SetClassProto` assertion** (`quickjs.c`): `MatWrapper::class_id_` is static, so `registerClass` only called `JS_NewClass` for the *first* `JsRuntime`. Each `JsEngine` owns its own runtime, so a second engine hit the assertion. Fix: gate `JS_NewClass` on `JS_IsRegisteredClass(rt, class_id_)` (not a static guard) so every runtime registers the class once. (`submodules/scripting/js_task/src/js_mat_wrapper.cpp`)
- **Error message missing**: `JsEngine::getLastError()` read the `stack` property first; in QuickJS `stack` holds only the backtrace (no message), so `throw new Error('test error')` lost the message. Fix: read `message` first, fall back to `stack`, then `toString`. (`submodules/scripting/js_task/src/js_engine.cpp`)
- **`cv.createMat` is width-first**: `createMat(W, H, C)` → cols=W, rows=H (per `brightness.js`), unlike the rows-first `cv::Mat` ctor. Covered by `mat_ops.js` in the graph tests.
- **`JsTask` leaked the `execute` JS function** (found by the new graph tests): `loadScript()` does `jsExecuteFunc_ = JS_DupValue(...)` but nothing freed it, so `JS_FreeRuntime` aborted under `DUMP_LEAKS`. Fix: add `~JsTask()` that `JS_FreeValue`s `jsExecuteFunc_` before the `JsEngine` (member) is destroyed. (`submodules/scripting/js_task/src/js_task.cpp`)

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

## GraphStudio crash reporting (Sentry + Crashpad, desktop only)

- **Stack**: `sentry-native` 0.16.2 with the `crashpad` backend. Depends on `app/graph_studio/third_party/sentry-native` (recursive git submodules incl. Chromium crashpad) cloned by **`scripts/fetch_sentry.sh`** (pinned tag, `third_party/` is gitignored). If the checkout is absent, CMake configures and builds **without** crash reporting (same "skip when dependency missing" convention as OpenCV-WASM). Not built for WASM/mobile.
- **Lifecycle**: `InitCrashReporting()` is called at the very start of `main()` in `app/graph_studio/src/platform/macos/entry.cpp` (before `QApplication`), `ShutdownCrashReporting()` before exit; both guarded by make for WASM. DSN comes from the `SENTRY_DSN` env var — no DSN ⇒ clean no-op (safe to run locally). Headers/impl: `app/graph_studio/include/CrashReporter.h` + `src/CrashReporter.cpp` (`sentry_options_new/..._set_dsn/_set_release/_set_environment/_set_handler_path/_set_database_path`, `sentry_init`, `sentry_close`).
- **Handler placement**: CMake copies the `crashpad_handler` binary next to the executable (`Contents/MacOS/` in the .app bundle on macOS; next to `graph_studio.exe` on Windows) at POST_BUILD, because crashpad launches it there. In-process settings include release `task_graph@0.1.0#<git-short>` + environment (production for Release/RelWithDebInfo builds, else development) baked by CMake compile definitions.
- **Manual verification**: `SENTRY_DSN=<dsn> graph_studio(.app/.../graph_studio) --test-crash` triggers a SIGSEGV; expect a crash issue in Sentry with symbolicated stack (upload symbols first, below). Without a DSN the flag still crashes but nothing is uploaded.
- **Symbols**: `scripts/upload_sentry_symbols.sh` (needs `sentry-cli` + `SENTRY_AUTH_TOKEN`) uploads `.dSYM`/PDB for manual symbolication. On macOS it auto-generates missing dSYMs via `dsymutil` from the `-g` Debug binaries (`app/graph_studio/build` + root `build/` — the latter covers `libtask_graph.dylib` and subnode plugins so crashes in the shared lib/plugins symbolicate too). Run it after each build that needs crash symbolification; for production releases keep DEBUG_INFO on and archive the dSYMs/PDBs.
- **Known limits**: macOS sandboxed/App Store distribution can't spawn a crashpad child handler (per doc, then use `native`/`inproc` backend); Windows main-thread stack overflow may not be captured without a thread stack-guarantee workaround.

## MediaPipe subnode (mediapipe_vision)

- `scripts/build_mediapipe_macos.sh` builds `libvision.dylib` (full MediaPipe framework, C API only exported) into `build/mediapipe/install/`. Requires `brew install bazelisk`; Docker on macOS only produces Linux binaries and cannot link into the macOS build.
- The script patches several upstream incompatibilities: zlib `TARGET_OS_MAC` fdopen (Xcode 26), eigen gitlab 403 (local override), OpenCV5 API (`getPerspectiveTransform` moved to `geometry/2d.hpp`, `boxPoints` removed, `calib3d`→`calib`/`features2d`→`features`, and `image_transformation_calculator.cc` needs `#include <opencv2/geometry/2d.hpp>` under `CV_VERSION_MAJOR >= 5` for `cv::getRotationMatrix2D`), and protobuf symbol conflicts. The `exported_symbols.txt` is now an **idempotent merge**: every run unions the existing list with the expected `Mp*` C API symbols (adds ~46 across the 10 vision tasks), so re-running the script never breaks previously-exported symbols. `VISION_LIBRARIES` lists 10 targets (face_detector, face_landmarker, gesture_recognizer, hand_landmarker, holistic_landmarker, image_classifier, image_embedder, image_segmenter, object_detector, pose_landmarker); interactive_segmenter is not included.
- The subnode CMakeLists links `libvision.dylib` when present in `build/mediapipe/install/lib`; otherwise it compiles a stub that makes no C API calls. **All ten vision tasks are now fully wired** (IMAGE mode): `run_image()` does Image→MpImage conversion then dispatches to a per-task `run_inference(MpImagePtr, VisionResult&, err&)` hook; each task's `on_init()` reads its options from config, calls the task-specific `Mp*Create` (handle reused across `execute()`, closed in the destructor), and `run_inference()` calls `Mp*DetectImage`/`Mp*ClassifyImage`/`Mp*EmbedImage`/`Mp*SegmentImage`/`Mp*RecognizeImage`, maps the C result → `VisionResult`, and frees via `Mp*CloseResult`:
  - **ObjectDetector** → `detections` (bbox normalized [0,1]).
  - **FaceLandmarker** → `face_landmarks` + optional `face_blendshapes` / `face_transformation_matrixes`.
  - **HandLandmarker** → `hand_landmarks` + `hand_world_landmarks` + `handedness`.
  - **PoseLandmarker** → `pose_landmarks` + `pose_world_landmarks` + optional `pose_segmentation_masks` (float32, copied via `MpImageDataFloat32` before `CloseResult`).
  - **FaceDetector** → `detections` (bbox normalized [0,1]).
  - **ImageClassifier** → `classifications` (per-head `vector<Category>`); options need the nested `classifier_options.max_results/score_threshold` (NOT top-level).
  - **ImageEmbedder** → `embeddings` (per-head `EmbeddingValue{head_index, values}`); with `l2_normalize` the embedding is unit-norm.
  - **ImageSegmenter** → `segmentation_masks` (float32 conf masks), optional `category_mask` (uint8), `segment_labels` (from `MpImageSegmenterGetLabels` + `MpStringList`). **C-API quirk**: `CppConvertToImageSegmenterOptions` assigns `display_names_locale` without a null-guard, so `on_init` must pass a non-null string (`"en"`), not `nullptr`, or `MpImageSegmenterCreate` segfaults.
  - **GestureRecognizer** → `gestures` (vector<vector<Category>>) + `hand_landmarks`; options need nested `canned_gestures_classifier_options`/`custom_gestures_classifier_options` with `max_results = -1` (0 is invalid).
  - **HolisticLandmarker** (class name `HolisticLandmarkerTask`, task id `mp_holistic_landmarker`) → pose/face/hand world+screen landmarks; `HolisticLandmarkerResult` fields are **single structs** (e.g. `left_hand_landmarks.landmarks` + `.landmarks_count`), no per-part `*_count`.
  - `running_mode` is currently hardcoded to IMAGE; VIDEO (`DetectForVideo`+`timestamp_ms`) and LIVE_STREAM (`DetectAsync`+callback) are not yet implemented.
- Test models **and images** are downloaded (not committed) via `scripts/download_mediapipe_models.sh` into `submodules/mediapipe/mediapipe_vision/tests/models/` (12 model files). The Face/Pose/ObjectDetector/FaceDetector/Classifier/Embedder/Segmenter/Holistic tests use a royalty-free portrait (`portrait.jpg`, Unsplash); the Hand/Gesture tests use a dedicated hand close-up (`hand_image.jpg`, CC BY-SA 4.0 by Adams890 via Wikimedia Commons — attribution in the download script).
- The ten `test_mediapipe_*` are **JSON-driven via the task_graph framework**: each reads a graph `<name>_graph.json` (loaded by `DAGSerializer`, executed by `DAGExecutor`) describing `opencv_image_read` (file_path) → `mp_*` (model_path + params) with a port edge `out→image`. The tests link both `mediapipe_vision` and `image_reader` so both task types register. Templates are committed as `tests/graphs/*.json.in` (with `@MODELS_DIR@` placeholders) and materialized into `<binary_dir>/graphs/` by `configure_file` (@ONLY), like every other submodule. Tests soft-SKIP when the downloaded model/image assets are missing (fresh checkout — run `scripts/download_mediapipe_models.sh`); they run CPU then GPU (GPU soft-SKIPs if the Metal delegate fails to init — common on macOS for landmarker models). Landmark range checks use a wide `[-5,5]` band because MediaPipe extrapolates out-of-frame body parts well beyond `[0,1]`.
- `libvision.dylib` ships with a **bare install name** (`libvision.dylib`, not `@rpath/...`), so LC_RPATH doesn't apply and `DYLD_LIBRARY_PATH` is stripped under SIP. The tests symlink the prebuilt next to the binaries via a one-shot `mediapipe_vision_libvision` custom target (rm+create_symlink, idempotent) and set `WORKING_DIRECTORY` so dyld's CWD lookup finds it. The real app/plugin loader must do likewise (copy/symlink or fix the install name).
- MediaPipe v1.0.0 has a `rules_java`/Bazel 7.4.1 incompatibility; the working build uses **v0.10.35** (`--version v0.10.35`).