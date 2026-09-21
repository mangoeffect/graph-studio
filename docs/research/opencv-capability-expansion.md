# opencv 子模块常用能力继续集成 — 调研与路线图

> 2026-09 调研。目标：梳理 `submodules/opencv/` 相对 OpenCV 常用能力的缺口，给出分阶段集成方案。
> 实施模式严格复刻 `image_geometry`（CMake 门控 / TG_PLUGIN_AUTOREG 双注册 / JSON 图驱动测试 / subnode.json 注册）。

## 1. 现状盘点

| subnode | 已覆盖任务 |
|---|---|
| image_reader / image_writer | `opencv_image_read/write` |
| image_filtering | blur 家族（blur/gaussian/median/bilateral/box/sqrBox/sepFilter2D/filter2D/gabor）、sobel/scharr/laplacian、dilate/erode/morphologyEx、pyrDown/pyrUp |
| image_geometry | resize/flip/rotate/warpAffine/transpose |
| image_color | cvtColor/threshold/adaptiveThreshold/applyColorMap |
| image_color_grading | wheels/curves/lut/bcs/mixer/hsl（达芬奇风格调色） |
| video_io | video_reader/video_writer |

GPU（wgpu）与 render 子模块已有 blur/grayscale/brightness/threshold/gamma/blend 等 GPU 版本；OpenCV 侧只补 **CPU 路径**，命名保持 `opencv_*` 前缀以示区分，不与 `gpu_*`/`render_*` 重复注册同一任务名。

## 2. 能力缺口矩阵

| 能力域 | OpenCV API | 优先级 | 需新跨端口类型 | 备注 |
|---|---|---|---|---|
| 直方图均衡 | `equalizeHist` | **P0** | 否 | 灰度；彩色自动转灰或分通道 |
| 自适应均衡 | `createCLAHE` | **P0** | 否 | clipLimit / tileGridSize |
| 锐化 | unsharp mask（Gaussian 差） | **P0** | 否 | amount/sigma |
| 去噪 | `fastNlMeansDenoising(Colored)` | **P0** | 否 | 仅 8UC1/8UC3；h/hColor |
| 图像混合 | `addWeighted` | **P0** | 否 | 双输入端口 in1/in2 |
| Gamma | LUT 幂律 | **P0** | 否 | 8U LUT |
| 亮度对比度 | `convertTo(alpha,beta)` | **P0** | 否 | 与 gpu_brightness_contrast 对齐的 CPU 版 |
| 反色 | `bitwise_not` | **P0** | 否 | — |
| 分割：grabCut | `grabCut` | P1 | 否（mask 即 Mat） | rect/mask 两种 init |
| 分割：watershed | `watershed` | P1 | 否 | marker 输入，32S label 输出 |
| 分割：floodFill | `floodFill` | P1 | 否 | seedPoint + loDiff/upDiff |
| 连通域 | `connectedComponents` | P1 | 否 | label Mat + num_labels 标量端口 |
| 距离变换 | `distanceTransform` | P1 | 否 | — |
| 特征点/描述子 | ORB / SIFT detectAndCompute | P2 | **是**：`KeypointsResult`（`TG_REGISTER_TYPE(..., "opencv::keypoints_result")`） | SIFT 4.4+ 在主库，无需 contrib |
| 特征匹配 | BFMatcher/FlannBasedMatcher | P2 | **是**：`MatchResult` | 依赖 P2 特征任务 |
| 轮廓 | `findContours` | P2 | **是**：轮廓点集 | 输出结构复杂，需求确认后再做 |
| 无缝克隆 | `seamlessClone` | P3 | 否 | photo 模块 |
| 边缘保留滤波 | `edgePreservingFilter`/`stylization` | P3 | 否 | photo 模块，偏风格化 |

**分界原则**：P0 全部是「Mat 进 Mat 出 + 扁平参数 + 无新类型」，零类型注册成本；P1 仍无新类型；P2 起需要 `TG_REGISTER_TYPE` 注册跨端口自定义类型（对齐 face/matting 的 `VisionResult` 先例），成本跃升一档，故放在需求确认后。

## 3. P0 落地：`image_enhance` subnode（本轮实现）

目录：`submodules/opencv/image_processing/image_enhance/`，8 个任务：

| 任务 type | 参数（扁平） | 输入 | 输出 |
|---|---|---|---|
| `opencv_equalize_hist` | — | in（自动转灰） | 8UC1 |
| `opencv_clahe` | `clip_limit`(2.0)、`tile_x`(8)、`tile_y`(8) | in（自动转灰） | 8UC1；tile<1 → FAILED |
| `opencv_sharpen` | `amount`(1.5)、`sigma`(1.0) | in | 同输入尺寸/通道 |
| `opencv_denoise` | `h`(3)、`h_color`(3)、`template_window`(7)、`search_window`(21) | in（须 8U） | 同输入；非 8U → FAILED |
| `opencv_add_weighted` | `alpha`、`beta`、`gamma` | **in1 + in2**（尺寸须一致，否则 FAILED） | 同输入 |
| `opencv_gamma_correct` | `gamma`(1.0，须 >0) | in（须 8U） | 同输入 |
| `opencv_brightness_contrast` | `brightness`(0)、`contrast`(1.0) | in | 同输入 |
| `opencv_invert` | — | in | 同输入 |

### 复刻清单（每个新 subnode 的既定模式）

1. 目录结构 `include/<name>/` + `src/` + `tests/{graphs,data}`；CMakeLists 从 image_geometry 拷贝改名。
2. 代码约定：`const char* const kXxxType` 常量、`type()` 返回 `static const std::string`、`TG_PLUGIN_AUTOREG` + `#ifndef __EMSCRIPTEN__` 守卫的 `register_plugin/unregister_plugin`（wasm whole-archive 撞名坑）。
3. `-fno-exceptions`（WASM/移动）：参数非法一律 `TaskResult{FAILED}`，不抛异常。
4. `subnode.json` 注册在 `image_reader` 之后（测试链接它）。
5. 测试：JSON 图（相对路径 `data/test.png`，CMake `file(COPY)` 到 `<binary_dir>/graphs/`）、每图一个 ctest 条目（`SKIP_RETURN_CODE 2`）、至少 1 个 FAILED 错误路径用例。
6. 验证：`scripts/run_tests.sh --opencv -R image_enhance`（桌面为准）；WASM/移动只保证 STATIC 编译。

## 4. P1/P2 路线图

- **P1 image_segmentation（已实施，2026-09）**：`submodules/opencv/image_processing/image_segmentation/`，5 任务全落地——
  - `opencv_grabcut`：`init_mode` RECT/MASK（MASK 经可选 `mask` 端口，GC_* 语义 8UC1）；输出前景二值 mask（GC_FGD|GC_PR_FGD → 255）。
  - `opencv_watershed`：可选 `markers` 端口（32SC1）；无上游 marker 时自动构建（边框带=1 背景、中心矩形=2 前景，rect 参数可覆盖）。输出 32S label 图（边界 -1）。
  - `opencv_flood_fill`：seed/new_value/lo_diff/up_diff/connectivity；`FLOODFILL_FIXED_RANGE` 语义；种子越界 → FAILED。
  - `opencv_connected_components`：**多输出** `labels`(32SC1 Mat) + `num_labels`(int，含背景)，经 `TaskResult.outputs`（matting 同款契约）。
  - `opencv_distance_transform`：L1/L2/C × mask 3/5/PRECISE，输出 32F。
  - **OpenCV 5 坑**：`DistanceTypes`(DIST_*) 已从 imgproc.hpp 迁到 `opencv2/geometry/2d.hpp`（同 `getPerspectiveTransform` 迁移），需 `#if CV_VERSION_MAJOR >= 5` 条件 include。
  - **WASM 坑（2026-09 实测）**：`opencv_denoise` 的 `fastNlMeansDenoising*` 属 **photo 模块**，而 `build_opencv_wasm.py` 默认只编 `core,imgproc,imgcodecs`——wasm 端编译直接失败。已按"依赖缺失优雅降级"惯例修复：`__has_include(<opencv2/photo.hpp>)` 编译期探测，缺失时任务保留注册、execute 返回 FAILED（同 MNN/mediapipe stub 语义）。若 wasm 端确需去噪，改 `--modules core,imgproc,imgcodecs,photo` 重建并同步 CI 缓存 key（体积代价需评估）。
  - 风险备忘：watershed 要求 32S marker（已通过可选端口 + 自动构建解决）；grabCut 迭代耗时 128×128 无碍，视频帧建议降 iterations。
- **P2 image_features**：ORB/SIFT + 匹配。需要 `KeypointsResult`（`std::vector<cv::KeyPoint>` 包装 + 稳定名 `opencv::keypoints_result`）与 `MatchResult` 两个 `TG_REGISTER_TYPE`；GraphStudio 面板对结构化类型的展示需确认（face 的 `FaceResult` 有先例）。SIFT 在 OpenCV 4.4+ 主库（无 contrib），ORB 恒可用。
- **P3 photo 风格化**：seamlessClone/edgePreservingFilter/stylization/pencilSketch —— 消费侧未定，暂缓。

## 5. 决策记录

- OpenCV 侧不重复做 GPU 已有能力？——**做 CPU 版**（`opencv_*` 前缀区分）：DAG 中 CPU/GPU 任务可混排，GraphStudio 以 type 名区分；命名空间不冲突。
- 不把新任务塞进 image_color/filtering 既有 subnode？——**新开 subnode**：既有 subnode 已各自成独立提交单元，粒度按 OpenCV 模块域聚合（enhance/segmentation/features）更清晰，也符合 `subnode.json` 每条目一目录的既有结构。
