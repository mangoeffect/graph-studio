# MediaPipe Windows CI 集成方案（阶段 2）

状态：**已实施**（提交见文末），待首个 main push 触发的 CI 验证冷构建与验收清单。
前置阶段 1（本地打通）见 AGENTS.md「MediaPipe prebuilt」节与子模块 plugin-mediapipe
`12f00c1`、主仓库 `d7adc1d`。

## 1. 现状与目标

四个 workflow（tests / graphstudio-build / release / e2e-windows）在 Windows 上原本全部构建
mediapipe_vision **stub**（无 `build/mediapipe/install/` 前缀时 CMake 自动降级），e2e 对
mediapipe 图按 allow-fail 处理。

目标：CI 上 Windows 产出并消费**真实** MediaPipe C API（v1.0.0，CPU-only）：

- tests.yml：10 个 `test_mediapipe_*` 真实推理 Passed（非 skip）
- release：MSIX 内置 vision.dll，安装态 e2e 的 mediapipe 场景 required 且通过
- 冷启动可控：只有 `scripts/build_mediapipe.py` 或 OpenCV 版本变化时才付一次冷构建成本

## 2. 关键设计决策

| # | 决策 | 理由 |
|---|------|------|
| D1 | **CI Windows OpenCV 统一为 4.10.0**（原 4.11.0） | vision.dll 构建期绑定 `opencv_world4100.dll`（本机即 4.10）。MSIX 打包从 `C:\opencv` 拷 opencv DLL 到包根——版本不统一则安装包内 DLL 与 vision.dll 导入表不匹配，加载即失败。统一到 4.10 而非让脚本适配 4.11，避免本地/CI 两种绑定并存 |
| D2 | **预构建获取 = actions/cache + 冷构建兜底**（配方已固化在 build_mediapipe.py） | 仿 sentry-native 模式。install/ 仅约 11MB；冷构建（全量 bazel ~3600 action）本机 12 核约 28 分钟，CI 4 核预估 1.5~3 小时，仅 cache miss 时发生 |
| D3 | **改动集中在 setup-build-deps composite action** | 四个 workflow 共用它且都不传参——OpenCV pin、缓存与冷构建放这里（新增 `mediapipe-win` input 开关），workflow 侧近零改动 |
| D4 | **vision.dll 放 MSIX 包根（exe 同级）** | PlugIns\ 里 mediapipe_vision.dll 的依赖解析走 loader 标准搜索（exe 目录 → system → PATH），不搜索 PlugIns 自身；包根已有同机制的 opencv DLL 先例 |
| D5 | **e2e 转正在打包修复之后，且补模型下载** | allow-fail 的前提是「安装包为 stub」；转正后 run_e2e_windows.py 必须先下载模型（场景资产与 configure 期复制都依赖它） |

## 3. 改动清单（已全部实施）

### 3.1 `scripts/build_mediapipe.py` — JDK 探测兜底
`windows_bazel_env()` 探测链：`tools/jdk` → `JAVA_HOME` → **`JAVA_HOME_21_X64` /
`JAVA_HOME_17_X64`**（windows-2022 runner 只保证带版本后缀的系列变量）。

### 3.2 `.github/actions/setup-build-deps/action.yml`
- `opencv-version-win` 默认 `4.11.0` → **`4.10.0`**（OpenCV 缓存 key 自动失效重下）。
- 新增 input `mediapipe-win`（默认 `true`），在 OpenCV 步骤后加两步（`runner.os == 'Windows'` 门控）：
  - `Cache MediaPipe prebuilt (Windows)`：path `build/mediapipe/install`，
    key `mediapipe-win-${{ inputs.opencv-version-win }}-${{ hashFiles('scripts/build_mediapipe.py') }}`。
    **不设 restore-keys**——错 ABI 的旧 DLL 比冷构建更危险。
  - `Build MediaPipe C API (cold cache)`：miss 时
    `python scripts/build_mediapipe.py --bazel-user-root C:/bzl`。

### 3.3 四个 workflow 的 timeout 修正（必要配套）
原 `setup-build-deps` 步骤的 `timeout-minutes: 25`（防 Vulkan 安装悬挂）会掐死冷构建：

- tests / graphstudio-build / release：步骤 timeout 25 → **300**（注释更新）。
- e2e-windows：步骤 timeout 25 → **300**；job timeout 90 → **360**（常规 <40min，
  冷构建 miss 时可达 3h+）。

### 3.4 `.github/workflows/tests.yml` — 模型下载
run 步骤改为 Windows 追加 `--download-models`（必须先于 cmake configure，
run_all_test.py 已保证顺序；模型 ~35MB 不缓存）。macOS/Linux 不加：无预构建
dylib/.a，仍为 stub、测试不注册，下载无意义。

### 3.5 `scripts/build_msix.ps1` — vision.dll 进包
- `PlugIns\` 收集排除 `vision.dll`（它在插件输出目录会被 `*.dll` 通配误收，10MB 死重）。
- `build/mediapipe/install/bin/vision.dll` 存在则拷到包根（缺失=stub 构建时跳过，不阻断）。

### 3.6 e2e 转正
- `run_e2e_windows.py`：build_msix 之前 `download_models(repo_root())`（幂等）。
- `scenarios_files.py`：删除 mediapipe 的 allow-fail（仅保留无 GPU 时的 gpu 图），
  与普通图同标准（`failed != 0` 即 fail）。GPU 用例在插件内已被
  `MEDIAPIPE_VISION_CPU_ONLY` 守卫为确定性 FAILED，无额外分支。

### 明确不改
- `graphstudio-build.yml` / `release.yml` / `e2e-windows.yml` 的依赖接入与打包调用：
  **零改动**（经 setup-build-deps 与 build_msix.ps1 自动获得）。
- 符号归档：release.yml 已 glob `build/submodules/*/RelWithDebInfo/*.pdb`，
  mediapipe_vision.pdb 自动覆盖（vision.dll 无 pdb，bazel `-c opt` 不产）。

## 4. 缓存分析

| 项 | 值 |
|----|----|
| 缓存路径 | `build/mediapipe/install`（约 11MB） |
| key | `mediapipe-win-<opencv-ver>-<hash(build_mediapipe.py)>` |
| 失效时机 | 脚本任意改动（含 MP_VERSION、补丁、flag）或 OpenCV 版本升级 |
| restore-keys | **不设**（错 ABI 旧 DLL 静默进包比冷构建危险） |
| bazel 输出基 `C:\bzl`（20GB+） | **不缓存**（超 GH 10GB/repo 配额，key 变化后命中价值低） |
| PR 行为 | main 缓存对 PR 可读 → 常规 PR 命中；仅改 build_mediapipe.py 的 PR 付冷构建（配方变更就该验证重建） |

## 5. 风险与缓解

| 风险 | 缓解 |
|------|------|
| 冷构建超时 | 步骤 300min / e2e job 360min，预估 ≤3h，余量足 |
| runner 磁盘（bazel ~20-25GB） | windows-2022 C: 常态余 30GB+；不足则冷构建步骤前清 Android/工具缓存 |
| runner MSVC 版本漂移（本机 14.36 验证） | 补丁为版本无关写法；首个冷构建即验证，破则补丁进脚本（key 自动跟随）或 pin `BAZEL_VC_FULL_VERSION` |
| 上游 google-ai-edge/mediapipe 漂移 | pin v1.0.0 tag + 浅克隆，仅主动升级时暴露 |
| OpenCV 4.10 回归 | 与 4.11 对本仓库 API 面无差异，三平台全量测试覆盖 |

## 6. 验收清单（首个 main push 后逐项核对）

- [ ] tests.yml Windows：`MediaPipe prebuilt found` 日志 + 10 个 `test_mediapipe_*`
      Passed 且输出 `[PASS] CPU: ... pipeline ok`（GPU 按退出码 2 软跳过）
- [ ] 二次运行 `mediapipe-cache` 命中（Setup 阶段无 bazel 输出）
- [ ] release Windows：MSIX 根目录含 vision.dll（~10.4MB）、`PlugIns\vision.dll` 不存在
- [ ] e2e-windows：`files/open:*mediapipe*` 场景 pass 且不再是 allow-fail 注记
- [ ] macOS / Linux 矩阵零行为变化
- [ ] 全链路绿后：AGENTS.md「MediaPipe prebuilt」节补一句 CI 约定
      （缓存 key、`--bazel-user-root`、OpenCV pin）

## 7. 后续可选（不在本阶段）

- macOS CI 构建 libvision.dylib、Linux `--docker` 产 .a（同一缓存机制），
  之后三平台统一开 `--download-models`。
- 官网/README 的 "MediaPipe vision (10 tasks)" 平台限定语更新（Windows 桌面从此成立）。
