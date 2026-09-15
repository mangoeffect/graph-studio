# face 子模块实现方案（人脸框 + 可选关键点；后端 mnn / mediapipe，可扩展）

> 2026-10 定稿（含用户三项调整）：①模块名 **face**，在 `submodules/` 建立独立子库；
> ②默认只开 `face_detect`（bbox）能力，**关键点由参数打开**；③完成后需验证
> **全平台集成**（macOS / iOS / Android / WASM）。

## 0. 结论速览

| 维度 | 结论 |
|---|---|
| 模块形态 | `submodules/face/face_detect/` 独立 git 子库（plugin-face），注册进 `subnode.json` + `.gitmodules`，编译期链接 subnode（桌面 SHARED / 移动 WASM STATIC） |
| 任务 | 单一任务类型 `face_detect`；**默认只输出人脸框**，`output_landmarks=true` 时叠加关键点 |
| 后端 | `backend` 参数 `auto`/`mediapipe`/`mnn`；引擎能力全部来自核心库公共 API（`MpVisionEngine` / `MnnEngine`），子库零引擎二进制依赖 |
| 扩展 | `IFaceBackend` + `register_face_backend()` 注册表，新后端（NCNN 等）= 一个 TU，零改任务层 |
| 降级 | 引擎 stub 惯例（产物缺失 → 任务照常注册、execute FAILED 可读错误）；`auto` 运行时按可用性降级，`FaceResult.backend` 可观测 |
| 验证 | macOS 桌面 ctest（真实推理）；iOS / Android / WASM 交叉构建集成（链接 + 任务注册面） |

## 1. 与既有设施的关系

- 两引擎已核心库直编（见 `docs/research/mnn-mediapipe-core-integration.md`）：
  `include/task_graph/{mnn,mediapipe}/` 是面向"后续子模块"的公共接入面——face 子库
  正是该设计的第一个消费者。`TASK_GRAPH_*_AVAILABLE` 是核心库 PRIVATE 宏，
  子库**不需要也不使用**它们：引擎头恒存在，stub 形态在 `create()` 运行时失败并
  返回可读 err，子库透传即可（天然全平台：WASM/iOS 上引擎 stub 时任务仍注册）。
- 既有 `mp_face_detector` / `mp_face_landmarker` 保持不变（引擎薄透传）；face 子库
  的差异 = 跨后端统一结果 + 检测/关键点一体化 + 后端可选可扩展。

## 2. 数据模型（`include/face/face_types.hpp`）

```cpp
namespace face_task {
struct FaceKeypoint { float x, y, z; float score{1}; };   // 归一化 [0,1]
struct FaceBox { float x_min, y_min, x_max, y_max; float score; };
struct FaceResult {
    std::vector<FaceBox> boxes;
    std::vector<std::vector<FaceKeypoint>> landmarks;  // 与 boxes 一一对应，可空
    int landmark_count_per_face{0};                    // 0=未开启，478/106...
    std::string backend;                               // 实际后端（auto 降级可观测）
    std::string landmark_scheme;                       // "mediapipe_478"（两后端同 scheme）
};
}
TG_REGISTER_TYPE(face_task::FaceResult, "task_graph::FaceResult");
```

归一化坐标对齐 MediaPipe 惯例；`landmark_scheme` 让下游区分点拓扑。

## 3. 后端抽象（`include/face/face_backend.hpp`）

```cpp
struct FaceBackendContext {        // 任务层解析好的候选模型路径与阈值
    std::string mp_detector_model, mp_landmarker_model;
    std::string mnn_detector_model, mnn_landmark_model;
    int max_faces{5};  float score_threshold{0.5f};  float nms_threshold{0.3f};
    bool output_landmarks{false};
    int mp_delegate{0};  std::string device{"auto"};  int threads{4};
};
class IFaceBackend {
public:
    virtual ~IFaceBackend() = default;
    virtual std::string name() const = 0;
    virtual bool detect(const task_graph::Image&, FaceResult&, std::string& err) = 0;
};
std::shared_ptr<IFaceBackend> create_face_backend(const std::string& backend,
                                                  const FaceBackendContext&, std::string& err);
bool register_face_backend(name, creator);   // 扩展点
void unregister_face_backend(name);
```

- 注册表为 name→creator 映射（互斥锁保护；创建经工厂函数调用，避开 render 滤镜
  踩过的"库单例构造期递归 instance()"坑）。
- `auto`：mediapipe → mnn 逐个尝试 create，全部失败时 err 聚合两路原因。

## 4. MediaPipe 后端（`src/face_mediapipe_backend.cpp`）

- `output_landmarks=false`：单 `FaceDetector`（`MpVisionTask::FaceDetector`，
  detections → FaceBox，`min_suppression_threshold` = nms_threshold）。
- `output_landmarks=true`：单 `FaceLandmarker`（自带检测，478 点含 iris），框由
  478 点 min/max 包围盒推导（官方短距检测器与 Landmarker 内置检测器同源，
  精度差异可忽略；score 无对应输出，取 1.0）。`landmark_scheme="mediapipe_478"`。
- GPU：`ctx.mp_delegate==1` 时先查 `MpVisionEngine::gpu_delegate_available()`，
  不可用回退 CPU 并在结果 err 注释里说明（CPU-only 构建请求 GPU 会挂死）。

## 5. MNN 后端（`src/face_mnn_backend.cpp`）

MNN 是通用张量引擎，模型与后处理归子库：

- **检测**（实测定稿）：UltraFace slim-320，**直接取 Linzaer 仓库官方 MNN 产物**
  （`MNN/model/version-slim/slim-320.mnn`，免转换；onnx 自转产物 score 语义有
  坑，见下）。预处理 mean 127 / scale 128、RGB（官方 MNN demo 同款）。解码 =
  SSD prior（**4 层**：steps {8,16,32,64}、min_sizes {{10,16,24},{32,48},{64,96},
  {128,192,256}}，320×240 输入共 4420 anchor）+ variance {0.1,0.2} 偏移回归 +
  NMS；**scores 已是概率（模型内融合 softmax），class1=face**。anchor 数在运行时
  与张量维度强校验（不匹配给出两侧数值的可读错误）。
- **关键点**：MediaPipe `face_landmark.tflite`（自 face_landmarker.task 解包）
  经 `mnnconvert --framework TFLITE` 转 .mnn——输入 **256×256** NHWC、[-1,1]、
  输出三张量（Identity=1434=478×3、score 标量、标志位），按尺寸挑 landmarks；
  布局为**交错 (x,y,z) 像素尺度**。检测框外扩 1.6× 方形 crop → 手写双线性
  resize → 推理 → 反映射归一化。`landmark_scheme="mediapipe_478"`（与 mp 后端
  同 scheme，含 iris）。
- **实测坑（2026-09 落地记录）**：
  1. `mnnconvert` 的 `--framework` 名必须**大写**（ONNX/TFLITE），小写报
     "Framework Input ERROR"；pip mnn 3.x 在本机 python3.14/3.12 上解析必挂，
     可用 build/mnn/mnn-src 源码构建的 MNNConvert 目标替代（脚本已做兜底）。
  2. **核心库 MnnEngine 的 ImageProcess 矩阵方向反了**（setScale 误取 dst/src，
     应为 src/dst = width/input_w）——非等比缩放下采样错位，UltraFace 上表现为
     置信度塌缩、框贴边；已修（对照实验：identity+预缩放 vs 倒置 scale 定位），
     mobilenet 用例无回归（251/251 全绿）。
  3. UltraFace onnx 自转产物与官方 .mnn 的 score 语义不同（自转版两类均饱和、
     无可用 softmax 语义）——直接用官方 .mnn 规避。
- crop/resize 子库内手写双线性，不引 OpenCV 强依赖。
- 线程约束沿用引擎：一实例不并发，DAG 并行分支各持任务实例。

## 6. 任务层（`face_detect`）

- 端口 `in`（Image | cv::Mat，type_name 留空）→ `out`（`FaceResult`）。
- 参数：
  - `backend` enum Auto/MediaPipe/MNN（默认 **Auto**）
  - `model_path` file（空 → 后端默认模型名：mp `blaze_face_short_range.tflite` /
    mnn `ultraface_slim_320.mnn`；经 ModelFinder → resolve_asset_path 解析）
  - `landmark_model_path` file（空 → mp `face_landmarker.task` / mnn 同前节）
  - `output_landmarks` bool **默认 false**（用户调整②：默认只有 bbox 能力）
  - `max_faces` 1..10 默认 5、`score_threshold` 默认 0.5、`nms_threshold` 默认 0.3
  - `delegate`（mp CPU/GPU）、`device`（mnn cpu/metal/auto）、`threads`
- `on_init()`：解析四个候选模型路径 + `create_face_backend`；失败写 init_error
  不致命（execute 再上报）。
- `execute()`：ensure_cpu → `detect()` → `TaskResult.value = FaceResult`；
  `-fno-exceptions`，只返回 FAILED。
- 注册：`TG_PLUGIN_AUTOREG` + `extern "C" register_plugin/unregister_plugin`
  （`#ifndef __EMSCRIPTEN__` 守卫，WASM whole-archive 同名冲突教训）。

## 7. 模型与测试

- `scripts/download_face_models.py`（主仓，惯例位置）：slim-320.mnn（官方 MNN
  产物直下）+ face_landmarker.task（官方地址解包 → mnnconvert TFLITE 转换），
  装 `tests/models/face/`（gitignored）。
- `tests/test_face_detect.cpp` + `tests/graphs/*.json.in`（ModelFinder 由驱动安装
  指向 tests/models/{mediapipe,face}）：mp 框、mp 框+关键点（478）、mnn 框、
  mnn 框+关键点（478）、auto 降级路径。模型缺失 → 软跳过。断言：≥1 脸、框归一化
  合理、关键点落在框邻域、mnn/mp 框 IoU>0.5（跨后端一致性）。

## 8. 全平台集成验证（用户调整③）

| 平台 | 验证内容 | 结果 |
|---|---|---|
| macOS | 真实推理：mp/mnn 双后端 bbox+关键点用例 + auto 降级 | ✅ 全仓 251/251 ctest 通过（face 5 图真实推理：mp 1 脸/478 点，mnn 1 脸/478 点） |
| iOS | `build_ios.sh`（face STATIC 并入 libtask_graph_full.a；MNN/MediaPipe 真实链接） | ✅ device+sim 切片构建通过，合并 13 库，`face_detect`/`FaceResult` 符号在位（nm 215 命中） |
| Android | `build_android.py --abi arm64-v8a`（同上） | ✅ 合并 23 库，face 符号在位（nm 21 命中） |
| WASM | `run_graph_studio_wasm.py --build-only`（STATIC + whole-archive；引擎 stub 路径） | ✅ `graph_studio.wasm` 19MB 构建通过，`face_detect`/`FaceResult`/`mediapipe_478` 字符串在产物中 |

平台接线改动：`scripts/build_ios.sh` 的子模块构建与合并列表、`scripts/build_android.py` 的
`SUBMODULE_TARGETS`（face 不依赖 OpenCV，OpenCV 缺失时也构建）各加一项；
`app/graph_studio/CMakeLists.txt` 的 WASM 子节点循环按 `subnode.json` 自动纳入，无需改。

顺手修掉交叉构建暴露的既有门控缺口：根 `CMakeLists.txt` 的 `test_js_script` 块原先只判
`TASK_GRAPH_ENABLE_OPENCV AND TARGET image_reader`，iOS+OpenCV 交叉构建时进入但
`gtest_discover_tests` 未定义 → 配置失败；已与 mp 测试块对齐改为
`NOT TASK_GRAPH_MOBILE AND NOT EMSCRIPTEN AND TASK_GRAPH_BUILD_TESTS ...`。

平台注意：face 子库**不在 mobile/WASM 上 return()**（与 render_task 不同）——
引擎 API 恒可编译；`-pthread` 对齐 wasm 多线程要求。

## 9. 仓管与 CI

- `submodules/face` = 独立 git 仓（本地 init，remote 预留
  `https://github.com/mangoeffect/plugin-face.git`）；`.gitmodules` +
  `subnode.json`（`face_detect` 条目，排在 image_reader 之后——测试链接它） +
  主仓 gitlink force-add（`submodules/` 树 gitignored 惯例）。
- **CI**：plugin-face 建私仓后需按轮换流程重建 `SUBMODULES_DEPLOY_KEYS`
  （新增一把 key；GitHub secret 只能整体重写，加仓=全量轮换，历史惯例）。
