# MNN / MediaPipe 直接集成主框架调研与实施记录（全平台二进制依赖）

> 2026-09 实施定稿。本文既是调研结论（各平台二进制获取矩阵、官方预构建可用性判
> 定依据），也是落地记录（文件清单、CMake 门控、坑与决策）。后续待实现的具体子
> 模块请直接消费第 2 节的公共引擎 API。

## 0. 结论速览

| 引擎 | 集成形态 | API 封装 | 桌面 | iOS | Android | WASM |
|---|---|---|---|---|---|---|
| MNN | 核心库直编（`src/mnn/`，先例） | `include/task_graph/mnn/mnn_engine.hpp`（本次公共化） | ✅ libMNN.a | ✅ Metal | ✅ per-ABI | ✅ 6.2MB |
| MediaPipe | 核心库直编（`src/mediapipe/`，本次自子模块迁入并移除目录） | `include/task_graph/mediapipe/vision_engine.hpp` + `vision_tasks.hpp` | ✅ macOS 实测 / Win 沿用既有 dll 链 / Linux 待产物 | stub（产物待交叉构建） | stub（同左） | stub（见 4.5 判据） |

- 两个引擎均以 **预编译二进制**（`build*/{mnn,mediapipe}/install` install 根）消费，
  源码不进主仓构建树（MediaPipe/MNN 的上游构建系统都不适合 add_subdirectory）。
- 找不到产物 → **stub 降级**：任务照常注册（图可反序列化/编辑），`execute()` 返回
  FAILED + "run scripts/build_mediapipe.py / build_mnn.py" 可读错误（对齐 MNN 惯例）。
- `submodules/mediapipe/` 目录、`subnode.json` 条目、`.gitmodules` 条目已移除；
  CI 部署密钥从 5 把减为 4 把（opencv, gpu, scripting, render，**需按轮换流程重置
  `SUBMODULES_DEPLOY_KEYS` secret**，见 6.3）。plugin-mediapipe 私有仓归档保留。

## 1. 公共引擎 API（后续子模块的接入面）

### 1.1 MNN —— `include/task_graph/mnn/mnn_engine.hpp`

```cpp
auto engine = MnnEngine::create(MnnEngineOptions{...}, err);
engine->run(image, outputs, err);   // 或 run(data,w,h,format,...)
```

- pimpl：真实 `MNN::` 头只在 `src/mnn/`，公共头 std-only。
- 线程约束：一实例不可并发（MNN 无跨线程 session 官方保证）；DAG 并行分支各持实例。
- `run(const Image&)` 便捷重载为本次新增（pixel_format → 通道序推导）。

### 1.2 MediaPipe —— `include/task_graph/mediapipe/`

```cpp
auto engine = MpVisionEngine::create(MpVisionTask::FaceLandmarker,
                                     MpVisionOptions{...}, err);
engine->run(image, vision_result, err);
```

- `vision_types.hpp`：`VisionResult` 等 7 个类型（`TG_REGISTER_TYPE` 稳定名，
  跨 SO 一致；已随核心库注册）。
- `vision_engine.hpp`：`MpVisionTask`（10 任务枚举）+ `MpVisionOptions`（union 风格
  全任务选项）+ `MpVisionEngine`（pimpl，Mp* 句柄生命期全在 `src/mediapipe/`）。
- `vision_tasks.hpp`：10 个 `mp_*` 任务类 = 参数解析 + 引擎委托的薄适配，是
  "后续子模块写法"的范本。
- `-fno-exceptions` 约定：API 只以 err 出参报错，不抛。
- IMAGE 模式；VIDEO/LIVE_STREAM 为后续路线（需 `DetectForVideo`/`DetectAsync` +
  时间戳/回调，引擎 Impl 的 switch 结构已预留扩展位）。

## 2. 为什么官方预构建不可直接用（依据）

- **桌面**：MediaPipe 官方不发布任何桌面 C++ 二进制（[平台构建文档](
  https://deepwiki.com/google-ai-edge/mediapipe/6.3-building-for-different-platforms)、
  [官方 Release 说明](https://github.com/google-ai-edge/mediapipe/releases/tag/v0.10.1)）。
- **iOS**：官方 `MediaPipeTasksVision.xcframework` 只导出 ObjC API；Android maven
  AAR 只导出 JNI/Java API——**都不含本项目任务层依赖的 Mp\* C API**（libvision 由
  Bazel 从源码构建时导出）。因此全平台统一走 "`scripts/build_mediapipe.py` 自构建
  → install 根当二进制消费"，与 MNN/OpenCV 同一约定。
- **Web**：官方只发 `@mediapipe/tasks-vision`（JS API over WASM），无法以 C ABI
  嵌入 C++ 核心（见 4.5）。

## 3. 落地清单（本次提交）

| 位置 | 内容 |
|---|---|
| `include/task_graph/mediapipe/{vision_types,vision_engine,vision_tasks}.hpp` | 公共类型 / 引擎 API / 任务层头（零 Mp 泄漏） |
| `src/mediapipe/{vision_engine.cpp,vision_tasks.cpp,mediapipe_registry.cpp}` | 引擎 pimpl（全部 Mp\* 调用集中于此）/ 任务薄适配 / 注册 + `pull_mediapipe_tasks()` 强拉锚点 |
| `include/task_graph/mnn/mnn_engine.hpp` | MNN 引擎 API 公共化（自 `src/mnn/mnn_engine.hpp` 上移，原内部头删除） |
| `src/data_types.cpp` | `TypeRegistry::instance()` 增加 mediapipe 锚点（STATIC mobile/WASM force-link，同 `pull_mnn_tasks` 手法） |
| 根 `CMakeLists.txt` | `TASK_GRAPH_ENABLE_MEDIAPIPE`（默认 ON）探针：`build{,_ios,_android,_wasm}/mediapipe/install`（Android per-ABI 优先），三形态 lib（dylib/so、`.a`、dll+lib）；`TASK_GRAPH_MEDIAPIPE_AVAILABLE` + `TASK_GRAPH_MEDIAPIPE_CPU_ONLY`；MSVC Debug genex（/MDd LNK2038，同 MNN/shaderc）；macOS POST_BUILD `install_name_tool -change` + 拷贝（见 5.1） |
| `tests/test_mediapipe_vision.cpp` + `tests/graphs/mediapipe/*.json` | 10 个图驱动用例合并为一个 gtest 二进制（原 10 个驱动逐条平移断言），模型走 `tests/models/mediapipe/`（`download_mediapipe_models.py` 已改指），缺失软跳过；门控镜像 `test_mnn_inference`（真实编入才注册、MSVC 多配置不注册） |
| 移除 | `submodules/mediapipe/`（git rm + .git/modules 清理）、`subnode.json` 条目、`.gitmodules` 条目 |
| CI | `fetch-private-submodules` action 改 4 把 key（顺序 opencv,gpu,scripting,render）；tests.yml 注释更新；`setup-build-deps` 的 `build/mediapipe/install` Windows 缓存原样有效（核心库探针路径未变） |
| 脚本 | `download_mediapipe_models.py` / `gs/models.py` / `run_e2e_macos.py` / `run_graph_studio.py` 模型目录改指 `tests/models/mediapipe`；e2e wasm/android 场景注释措辞更新；`run_all_submodules_test.py` 去掉 mediapipe_vision 映射 |

**macOS 实测**：全仓 236/236 ctest 通过（mediapipe 10 用例真实推理、MNN 用例真
实推理）；stub 通道（藏掉 install 根）编译通过且任务注册符号在（27 个导出）。

## 4. 平台矩阵与后续路线

### 4.1 macOS（✅ 已闭环）
`build_mediapipe.py` → `libvision.dylib`；dylib 分发问题见 5.1。

### 4.2 Windows（链路已有，待 CI 复验）
`build_mediapipe.py` 的 MSVC patch 链（protobuf/api3/opencv5 hunks）+ setup-build-deps
的缓存冷构建。核心库链接 vision.dll+vision.lib，POST_BUILD 拷 vision.dll 到
libtask_graph.dll 旁。Release-only + Debug genex。

### 4.3 Linux（✅ 链路闭环，产物待 CI 缓存）
`build_mediapipe.py`（Linux 本机原生 bazelisk；macOS 宿主上 `--platform linux` 走
ubuntu:22.04 Docker，容器内 apt JDK+libopencv-dev、bazelisk 按宿主架构下载、
`--output_user_root` 指到挂载目录）→ `libvision.so`（探针已支持 `.so` 形态）。
非 Linux 宿主的 Docker 产物装 `build/mediapipe/install-linux`（不踩桌面 install）。
CI：tests.yml ubuntu job 已加 `mediapipe-linux-*` 缓存 + 冷构建步骤
（libopencv-dev + JAVA_HOME_17_X64 + 钉版 bazelisk），mp 用例随 `--download-models`
真实执行。风险：桌面 Linux 可开 GPU delegate（官方唯一支持平台），当前仍按
CPU-only 构建，后续可评估。

### 4.4 iOS / Android（✅ 交叉构建已落地）
- `build_mediapipe.py --platform ios` / `--platform android --android-abi <abi>`：
  构建 `libvision.dylib`/`libvision.so` cc_binary（目标平台配置）强制物化全部传递
  依赖，再收集合并为 `libmediapipe_vision_c.a`（iOS libtool / Android NDK
  llvm-ar MRI）。安装根 `build_ios/mediapipe/install`、
  `build_android/mediapipe/install/<abi>`，CMake 探针已就位。
- **iOS 三个实测坑**：
  1. apple 工具链下 cc_library 产物是 `.lo` 单对象而非 `.a`（直接 build cc_library
     目标只会物化顶层 .lo，传递依赖不归档）——必须构建 cc_binary 触发链接，
     然后从 `bazel-out/ios_arm64-opt` 收集全部 `.lo`/`.a`；
  2. 打包 `ios_opencv`（3.2 静态 framework）是五架构胖子——`lipo -thin arm64`
     后再并入，否则合并库被撑成 fat；
  3. 合并后 `strip -S` 去调试段（1.3GB → 87MB）。C API 命中 OpenCV 3.2 的旧签名
     （`getPerspectiveTransform` 2 参、`points(Point2f[])`），源码补丁按
     `CV_VERSION_MAJOR` 版本自适应（>=5 / >=4 / 3.x 三态）。
- **Android**：WORKSPACE 追加官方配方（`android_ndk_repository` + `bind
  android/crosstool → @androidndk//:toolchain` + `register_toolchains`）——bzlmod
  下上游 WORKSPACE 的 ndk 仓库声明标了 `@unused` 从未注册，`--config=android_arm64`
  直接 "Unable to find a CC toolchain"。
- 合并库不带系统依赖：CMake 静态导入目标补 INTERFACE（Android `-llog -landroid`；
  iOS Foundation/CoreGraphics/CoreMedia/CoreImage/Accelerate/AssetsLibrary/
  AVFoundation/QuartzCore/CoreVideo，即 opencv_ios.BUILD 的 linkopts 清单）。
- 平台脚本已接线（warn-tolerant ensure + 合并）：build_ios.sh（device 并入
  `libtask_graph_full.a`，sim slice 显式 `-DTASK_GRAPH_ENABLE_MEDIAPIPE=OFF`——
  库是 device-only）、build_android.py（并入 `dist/android/<abi>/libtask_graph.a`）。
  CI ios/android job 已加 mediapipe 缓存 + 前置构建步骤。
- 官方 xcframework/AAR 只能作为**对照基准或模型格式参考**，不能直接当链接输入。

### 4.5 WASM（判据化降级）
- 路线 A：Bazel `--config=wasm` 交叉构建 libvision C API。已知冲突面：钉版
  emsdk 3.1.37 vs MediaPipe wasm 工具链要求；`-fno-exceptions`（protobuf/tflite
  需异常关闭补丁）；产物体积（预计 ≥ 数十 MB）。
- 路线 B（当前）：WASM stub——mp_* 任务注册 + 可读错误；图的 wasm e2e 场景按此
  标注。若需浏览器端真实推理，短期更现实的是 JS 桥接任务（包装官方
  `@mediapipe/tasks-vision` JS API，经 scripting/宿主 JS 层编排），不进 C++ 核心。
- 决策判据：路线 A 先做一次体积+工具链 spike（1-2 天）；abi3/异常补丁面过大或
  体积 >30MB 即维持路线 B。

## 5. 关键坑（本次实测）

### 5.1 macOS dylib bare install name（三连坑）
1. dyld 对**裸名**依赖（`libvision.dylib`，无 `@rpath/` 前缀）**不查 LC_RPATH**——
   给 libtask_graph 加 rpath 没用（全仓 71 例 dyld 加载失败就是这样来的）。
2. 链接器（ld64）不允许在 `-dynamiclib` 上用 `-change` 改写依赖记录。
3. **解法**：POST_BUILD `install_name_tool -change libvision.dylib
   @rpath/libvision.dylib` + `BUILD_RPATH "@loader_path"` + 拷贝 libvision.dylib
   到 libtask_graph.dylib 旁。dyld 以"声明依赖的镜像"所在目录解析 @rpath，测试
   exe（build/）与子模块插件（build/submodules/*，dlopen 加载核心库）全部命中，
   旧子模块时代的 per-test symlink hack 消亡。打包侧 dylibbundler 沿依赖表自然
   收集。
### 5.2 stub 分支的 pimpl 完备性
`unique_ptr<Impl>` 析构需要完整类型：stub 分支（无 `TASK_GRAPH_MEDIAPIPE_AVAILABLE`）
必须给 `MpVisionEngine::Impl` 一个空定义，否则编译失败。
### 5.3 C API 命名不统一
ImageEmbedder 的选项/结果是**无前缀**的 `ImageEmbedderOptions`/`ImageEmbedderResult`
（其余 9 任务都是 `Mp*` 前缀）——v1.0.0 C API 的历史遗留。
### 5.4 测试资产路径
图夹具从 `tests/graphs/mediapipe/` 引用 `../../models/mediapipe/...`（base_dir 起
算），ModelFinder 注册到 `tests/models/mediapipe`；源码树/GraphStudio 拖拽两用。
### 5.5 GPU delegate
桌面预构建恒 CPU-only（`--define=MEDIAPIPE_DISABLE_GPU=1`）：请求 GPU 不报错而是
**挂死** graph 创建，引擎 `create()` 前置拦截（`TASK_GRAPH_MEDIAPIPE_CPU_ONLY`）。
测试的 GPU 用例按此软跳过。

## 6. 运维注意

### 6.1 产物版本防混代
沿用 `.mp-build-version` sentinel：`build_mediapipe.py` 重跑时版本不一致即重克隆
（2026-09-13 三代混装事故的教训；headers/dylib 必须同源同版本）。交叉构建扩展
时每平台 install 根都要带 sentinel（MNN 已有同类先例）。

### 6.2 CI
- Windows：`setup-build-deps`（mediapipe-win 输入）缓存 `build/mediapipe/install`
  （key 含 `hashFiles('scripts/build_mediapipe.py')`）不变，核心库自动链入。
- macOS/Linux：当前无 dylib/.so 产物 → mp 用例不注册（门控），与 MNN 的
  "macOS/Linux 真实执行、Windows 编译验证"互补；Linux 产物就位后补 cache 步骤。
- 模型下载（Windows `--download-models`）：下载脚本已改指 `tests/models/mediapipe`。

### 6.3 部署密钥轮换（**待运维执行，一次性**）
`SUBMODULES_DEPLOY_KEYS` secret 当前仍是 5 把拼接；mediapipe 移除后需按
AGENTS.md 轮换流程重新生成 **4 把**（顺序 opencv, gpu, scripting, render）并重设
secret——action 侧已按 4 把校验，不轮换则所有 CI fetch 失败。删除 plugin-mediapipe
仓库上的旧 deploy key 可选（归档仓不再被 CI 访问）。

## 7. 开放项 / 后续路线

1. `build_mediapipe.py` 交叉构建扩展（Linux docker → iOS → Android），每平台带
   sentinel；build_ios.sh / build_android.py / run_graph_studio_wasm.py 的
   ensure 接线（warn-tolerant，镜像 MNN）。
2. WASM 路线 A/B 判据 spike（4.5）。
3. VIDEO / LIVE_STREAM running mode（引擎 switch 预留）。
4. e2e 图夹具（e2e_graph_cases.py 的发现范围目前只扫 `submodules/*/tests/graphs`，
   mp 图已随迁移离开该范围；如需 e2e 覆盖，把发现逻辑扩到 `tests/graphs`）。
5. MNN Windows /MDd 变体（当前 Debug 恒 stub，LNK2038 约束）。
