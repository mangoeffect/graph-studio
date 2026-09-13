# Android 端 graph E2E（adb + tg_e2e_runner）

对齐目标：macOS 安装态 E2E（`dev-docs/e2e-macos.md`）的**机制**——图发现/落位、
`[gs]` 完成行断言、每图冷启动隔离、共享报告系统；覆盖**全部可跑的 submodules
单测图**。不对齐的部分是 GUI 与安装态：Android 侧仓库只产出静态库 SDK
（`dist/android/arm64-v8a/libtask_graph.a`），没有 GraphStudio 的 app 形态。

## 选型：为什么是无头 native runner，不是 Qt for Android APK

Android 侧现状（调研结论）：无 `app/` 平台目录、无 AndroidManifest/APK 工程、
无 gradle；`scripts/build_android.py` 只做"NDK 交叉编译 + llvm-ar 合并静态库"。
在这之上做 APK 需要：Qt for Android 工具链、jniLibs 装箱、签名、触屏化的
UI 适配（桌面菜单栏/右键菜单/拖拽在触屏上没有等价交互）、app 沙箱文件读权限
——周级工作量，且 core/lifecycle 那类 GUI 手势场景在触屏上本来就要重设计。

因此 Track 5 先落"**图执行**"这一半（也是子模块覆盖的全部价值所在）：一个
console 程序承担 macOS 侧 `graph_studio --open <json> --run` 的角色。等价关系：

| 机制 | macOS | WASM | Android |
|---|---|---|---|
| 图输入 | CLI `--open/--run` | URL `?open=&run=1` | runner argv（图路径） |
| 断言 | stderr 镜像文件的 `[gs]` 行 | 浏览器 console 的 `[gs]` 行 | runner stdout 的 `[gs]` 行 |
| 冷启动 | 每图一个进程 | 每图一个新 tab | 每图一次 `adb shell` |
| 计数锚点 | AX 的 `Nodes: N \| Edges: M` | `__gsTest.taskCount()` | 日志行 `Graph loaded: N nodes, M edges` |
| 裁剪规则 | `e2e_graph_cases.select_graphs`（三端共享） | 同 | 同 |
| 报告 | `e2e_report.py` → `dist/e2e_macos/` | → `dist/e2e_wasm/` | → `dist/e2e_android/` |

## 架构

```
主机（macOS/Linux）                     设备（/data/local/tmp/gs_e2e/<stamp>/）
┌───────────────────────────────┐       ┌──────────────────────────────────────┐
│ run_e2e_android.py            │       │ tg_e2e_runner（去符号副本，14MB）     │
│  e2e_android/adb.py           │ adb   │ submodule_graphs/<module>/<图+资产>   │
│  e2e_android/scenarios.py     │ ────► │ smoke/e2e_graph.json                 │
│  e2e_graph_cases.py（共享）    │ push  │   ↑ 每图：                              │
│  e2e_report.py（共享）         │       │   adb shell <runner> <图的绝对路径>    │
└───────────────────────────────┘       └──────────────────────────────────────┘
        ▲ stdout（[gs] 行）经 adb 管道回收 → app_logs/<module>_<图>.log
```

## 应用侧通道：tests/android（runner）

`tests/android/tg_e2e_runner.cpp`（约 190 行，`tests/android/CMakeLists.txt`
定义 `EXCLUDE_FROM_ALL` 目标，只由 `build_android.py --e2e` 构建）：

- `tg_e2e_runner <graph.json> [--threads N]` — 读图（`DAGSerializer::from_string`
  带 base_dir = 图所在目录，图内相对资产由 `resolve_asset_path` 解析）→
  `DAGExecutor` 执行 → 输出 `[gs]` 行；退出码 0=全成功 / 1=有失败 / 2=图加载失败。
- `--selfcheck` — 打印 `[gs] selfcheck: abi=<ABI> tasks=<N>` + 每个已注册任务类型
  （驱动侧 boot 场景据此断言 ABI 匹配与 **whole-archive 注册保活**）。
- `--test-crash` — `std::raise(SIGSEGV)`（对齐桌面 `--test-crash`）。

输出契约与桌面 app 的 `qInfo` 镜像**逐字一致**（`GraphViewModel` /
`MainWindow::onLogMessage`），驱动侧的 `FINISHED_RE` 三端共用同一正则：

```
[gs] Graph loaded: 3 nodes, 2 edges
[gs] img  (4.03 ms)                  # 对齐 finishExecution 的 "%1  (%2 ms)"
[gs] blur: <failure_reason>          # 对齐 onExecutionEvent 的失败行
[gs] task 'err' failed: JS: test error   # Android 附加诊断行（见下）
[gs] Execution finished: 3 ok, 0 failed
```

统计口径也照抄 app：`is_success()` 计 ok，其余（FAILED/SKIPPED/PENDING）计 failed。

**附加诊断行**（`task '<id>' failed: <消息>`，超出 app 日志契约的增量、不影响
`FINISHED_RE`）：任务异常消息只存在 `TaskResult::exception` 里——executor 对
"`execute()` 返回 FAILED"这条路径发的 `TaskFailed` 事件不带 reason（app 侧同样
只是裸 id 一行）。Android 端没有 AX 现场快照/失败截图可比对，这条是失败定位的
主要依据（实测 js_error 图输出 `task 'err' failed: JS: test error`），`__cpp_exceptions`
守卫以兼容无异常构建。

### 链接：静态注册保活（本方案最容易静默失效的一环）

Android 是 STATIC 核心库 + 静态子模块，子模块靠
`TG_PLUGIN_AUTOREG` 的 `__attribute__((constructor))` 注册——archive 里无外部
引用的 TU 会被链接器直接丢掉，注册随之消失**且不报错**。三个链接选项缺一不可：

- `-Wl,--whole-archive`：强制全量链接子模块（WASM app 同款先例）；
- `-Wl,--start-group ... --end-group`：core 与子模块互相引用，一次性顺序扫描
  会漏解；
- `-Wl,--allow-multiple-definition`：每个子模块都导出同名的
  `extern "C" register_plugin/unregister_plugin`（桌面 dlopen 入口）。WASM 走的
  是 `#ifndef __EMSCRIPTEN__` 源码守卫（wasm-ld 无此选项），Android 的 ld.lld
  支持该选项——不动子模块源码，且 Android 不做 dlopen，取任意一份即可。

`--selfcheck` 把注册结果变成可断言的事实：实跑 39 个任务 = 35（subnode.json 的
7 个 Android 模块）+ io_input/io_output（核心库）+ mnn_inference/
mnn_image_classifier（MNN）。

## 驱动侧

- `scripts/e2e_android/adb.py` — `AdbDevice`：adb 发现（PATH → `ANDROID_HOME`/
  `ANDROID_SDK_ROOT` → 平台默认路径）、设备选择（`--serial`；缺省要求恰好一台
  在线设备，多设备报错而不猜）、`shell/push/pull/getprop/wait_device`。设备命令
  一律 **argv 列表**传递，从不拼 shell 字符串。
- `scripts/e2e_android/scenarios.py` — 场景与 `DeviceRunner`（= macOS 的
  `AppRunner` 角色）。
- `scripts/run_e2e_android.py` — 入口（CLI 对齐 macOS：`--scenario/--serial/
  --artifacts/--max-graphs/--graphs`，另加 `--runner/--abi/--no-build`）。

`--graphs`/`--max-graphs` 的裁剪语义与 macOS/WASM 完全同一份实现
（`e2e_graph_cases.select_graphs`，本次顺手把两端重复的内联版本收敛过去）。

## 场景

| 场景 | 覆盖 | 断言 |
|---|---|---|
| boot | 设备就绪、runner ABI、插件注册 | `wait_device` + `--selfcheck`：ABI 与 `ro.product.cpu.abi` 一致、subnode.json 的预期任务全在 |
| run | 单图冒烟（生成的 64×64 tiny PNG） | 恰好 `1 ok, 0 failed` |
| files | 全部可跑子模块单测图逐张冷启动 | `Graph loaded` 计数 = 图内 tasks/edges；`failed == 0` |
| crash | `--test-crash` | 退出码 139（128+SIGSEGV）/ -11 |

模块 skip（`ANDROID_UNSUPPORTED_MODULES`，机制对齐 WASM 的
`WASM_UNSUPPORTED_MODULES`）：

- `image_processing`（= gpu 子模块）：无 Android GPU 后端——wgpu 预编译产物
  只有 win/macos/linux，Vulkan 走桌面 SDK 的 `find_package`，其 CMakeLists 在
  `TASK_GRAPH_MOBILE` 下直接 `return()`；
- `render_task`：同上（Android 构建门未开）；
- `mp_*`（核心库）：无 Android libvision 预编译库（`build_mediapipe.py` 无
  Android 支持），任务是 stub；
- `video_io`：OpenCV Android 预编译的 `BUILD_LIST` 只有 core/imgproc/imgcodecs。

实跑（arm64 AVD，Android 13）：**27 张通过**（opencv 全系 25 + js_task 2，与
WASM 端实跑口径一致）+ 52 张 skip；boot/run/crash 全绿。

## 运行

前置：

1. `export ANDROID_NDK=$HOME/Library/Android/sdk/ndk/<ver>`（`gs.android.find_ndk`
   只读环境变量，不做默认路径探测）；
2. OpenCV Android 预编译：`python3 scripts/build_opencv_android.py`（已有
   `build_android/opencv/install` 则跳过）；
3. arm64 模拟器（Apple Silicon 原生）或真机：
   `emulator -avd <name>`（AVD 需与 runner ABI 一致；x86_64 模拟器用
   `build_android.py --also-x86-64` 建 x86_64 runner 并 `--abi x86_64`）；
4. 可选：`build_android/mnn/install/<abi>`（缺则核心库以 stub 编译，MNN 任务
   不注册——不影响 opencv/js 图）。

```bash
python3 scripts/build_android.py --e2e            # SDK 产物 + runner（含去符号副本）
python3 scripts/run_e2e_android.py                # 全场景（runner 缺失时自动构建）
python3 scripts/run_e2e_android.py --scenario boot,run
python3 scripts/run_e2e_android.py --max-graphs 10
python3 scripts/run_e2e_android.py --graphs image_filtering
python3 scripts/run_e2e_android.py --serial emulator-5554
```

报告落 `dist/e2e_android/<时间戳>/`：`summary.json`/`summary.md` + `app_logs/`
（每图 runner 输出）+ `submodule_graphs/`（图与资产副本）+ `smoke/`。退出码
0=全通过，1=有失败，2=环境不就绪（无 adb/无设备/推送失败）。

## 实测坑（都踩过）

- **runner 带 `-g` 时 137MB，模拟器 `/data` 装不下**：Pixel_3a AVD 的 data 分区
  只有 774MB；NDK 默认给 Release 也加 `-g`（`-O3 -DNDEBUG -g`）。`--e2e` 因此
  额外产出 `tg_e2e_runner.stripped`（`llvm-strip --strip-debug`，14MB），驱动
  优先推它（原文件留给崩溃符号化）。另外每次运行要清**整个**
  `/data/local/tmp/gs_e2e`（不止本次 stamp），否则历次残留迟早把分区塞满，
  表现为 adb push "No space left on device"。
- **adb shell 的退出码语义不必依赖**：完成断言以 `[gs]` 完成行为准（与
  macOS/WASM 同一判据），退出码只作失败诊断的补充信息。
- **`adb push <dir> <remote_root>`** 在 `remote_root` 已存在时落到
  `<remote_root>/<dir 名>`——落位树按此保持 `submodule_graphs/`、`smoke/` 同名。
- **设备侧日志不要用重定向 + shell 字符串**：驱动改为 argv 直调 runner、主机侧
  写日志文件（每图的 `app_logs/<module>_<图>.log` 与 macOS 同命名）。
- **runner 的 stdout 与框架 logger 同流**（INFO 日志会混在 `[gs]` 行之间）：
  断言用正则搜行即可，日志尾巴还能当失败诊断。

## 已知限制（未做）

- **Qt for Android APK / 安装态 E2E**：core/lifecycle 那类 GUI 手势场景不覆盖
  （Android 无 app 可驱动）。若将来做 APK，图输入通道建议沿用 intent extra +
  同一个 `OpenGraphAtStartup`，断言通道仍走 `[gs]` 行（logcat/tombstone 侧）。
- **gpu/render/mediapipe 的 Android 后端化**未做（wgpu 需 android 产物或
  Vulkan-on-Android 接线；mp_* 需 Android 交叉编译 libvision）——这四类
  模块的图在 Android 记 skip，不是"失败"。
- **CI 未接线**：对齐 WASM/macOS E2E 的本地轨道定位；进 CI 需要 runner 无设备
  运行（host 侧跑不了 aarch64 二进制）或引入模拟器 action，未评估。
- 执行是**串行**的（对齐 macOS），并行化（多设备/多进程）留作后续。
- `dist/android/libtask_graph.a` 的合并清单未纳入 js_task/quickjs（runner 单独
  链接它们）——SDK 发行面变更另议。
