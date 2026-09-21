# Photoshop 混合模式（blend 子模块）调研与设计

- 日期：2026-09-20
- 状态：已实现（submodules/blend，本地仓库已建立；CI 接入待远端建仓后轮换 key）
- 官方参考：[Adobe 混合模式说明](https://helpx.adobe.com/photoshop/desktop/repair-retouch/adjust-light-tone/blending-mode-descriptions.html)（tw 版）
- 规范参考：[W3C Compositing and Blending Level 1 §10](https://www.w3.org/TR/compositing-1/#blending)

## 1. 调研结论

### 1.1 模式清单（6 群组 27 个图层混合模式）

| 群组 | 模式 | 备注 |
| ---- | ---- | ---- |
| 正常 | Normal, Dissolve | |
| 变暗 | Darken, Multiply, Color Burn, Linear Burn, Darker Color | Adobe 文档称"减去 (Subtractive)"群组 |
| 变亮 | Lighten, Screen, Color Dodge, Linear Dodge (Add), Lighter Color | Adobe 文档称"增加 (Additive)"群组 |
| 对比 | Overlay, Soft Light, Hard Light, Vivid Light, Linear Light, Pin Light, Hard Mix | |
| 比较 | Difference, Exclusion, Subtract, Divide | |
| HSL | Hue, Saturation, Color, Luminosity | |

**不实现**：Behind（后面）、Clear（清除）——Adobe 文档明确它们只在绘画工具的混合模式列表
中出现（依赖画笔 alpha 绘制语义，非图层合成）。32-bpc 下部分模式不可用（Burn/Dodge 系、
Vivid/Pin/Hard Mix 等）是 HDR 通道语义问题，与本实现无关（我们只支持 UINT8）。

### 1.2 公式来源与裁断

- **16 个可分离模式 + HSL 四模式的助手函数**（Lum/Sat/ClipColor/SetLum/SetSat，含 soft-light
  的 D(x) 分段多项式）取自 W3C 规范 §10。注意 W3C 的 soft-light 最终稿采用的就是 Photoshop
  兼容公式（D(x) = ((16x−12)x+4)x for x≤0.25 else √x），两者一致。
- **Photoshop 扩展模式**（Linear Burn/Dodge、Darker/Lighter Color、Vivid/Linear/Pin Light、
  Hard Mix、Subtract、Divide）Adobe 用户文档只有描述性文字，公式采用业界公认实现。
- **两处官方文字与实测行为的差异，按实测裁断**：
  1. **Hard Mix**：官方文字为"把混合色 RGB 值加到基本色 RGB 值上，和 ≥255 则 255、<255 则 0"
     ——字面实现是 Linear Dodge + 阈值；实测（及 GIMP/Krita 等实现）为 **Vivid Light 结果
     ≥0.5 二值化**（例如 50% 灰叠 50% 灰，字面法得白、实测法得黑）。本实现取实测公式。
  2. **Darker/Lighter Color**：官方文字"比较所有色版值的总和"按字面实现（R+G+B 之和，与
     luma 权重不同）。此处官方文字与实测一致，按字面实现。
- **Divide**：除数为 0（纯黑混合层）时输出白（Photoshop 实测语义：除黑变白）。
- **数值域**：Photoshop 8-bit 管线对每个中间步骤做整数舍入；本实现是**全程 f32 单次舍入**
  （最后量化一次）——与 PS 逐位结果可有 ±1 差异，这是文档化的设计差异（GPU/CPU 双路径
  内部则逐位一致，见 §3.4）。

### 1.3 opacity 与 alpha 语义（本实现的合成模型）

Photoshop 图层混合：blend result 按 **图层不透明度 × 混合层 alpha** 调制后落在基底上。

```
fa    = opacity × (blend 层带 alpha ? a_blend : 1)
Dissolve: fa 逐像素二值化（确定性哈希 hash01(p,seed) < fa ? 1 : 0）
out_rgb = base + (B(base, blend) − base) × fa
out_a   = a_base + fa × (1 − a_base)      （仅 4ch 输出）
```

- 1ch（灰度）输入复制为灰 RGB 参与公式，输出取 `lum(out_rgb)`（HSL/lum 公式在灰度上退化成立）。
- 3/4ch 按框架惯例解释为 BGR/BGRA（`PixelFormat::RGB==BGR==3`，枚举只编码通道数，
  OpenCV 生产者主导；`gpu_color_ops` 的 bgr 标志同款推导）。
- 输入通道 1/3/4 任意组合；输出布局（通道数、字节序）跟 base。
- 字节量化：`round-to-nearest`（floor(clamp01(v)×255+0.5)）。**不用截断**：/255 浮点回程
  误差 ~1e-5 会被截断稳定压低 1，round 则被 0.5 边界完全吸收（normal/dissolve@opacity1
  可字节级恒等）。

## 2. 架构设计

### 2.1 布局

```
submodules/blend/            ← 独立 git 仓库（本地 init，remote 预留 plugin-blend）
  blend/                     ← subnode 目录（subnode.json 注册名 "blend"）
    include/blend/blend_modes.hpp   公共定义：Mode 枚举（= kernel mode id）+ 公式层 API
    src/blend_modes.cpp             模式表 + separable/非分离公式（CPU 参考的公式层）
    src/blend_cpu.cpp               CPU 全图实现
    src/blend_gpu_op.cpp            GPU compute op "blend"（WGSL/MSL/GLSL 三源）+ 注册
    src/blend_task.cpp              BlendTask：GPU 优先自驱动 + CPU 兜底 + 插件注册
    tests/…                         JSON 图 ×17 + sweep + 确定性资产（PIL 生成脚本入库）
```

### 2.2 GPU 优先策略（用户契约：默认优先 GPU，GPU 里优先 WebGPU）

- 插件**不自链接任何 GPU 产物**：kernel 是注册进核心 `GpuKernelLibrary` 的源码数据（op 名
  `blend`），执行走核心运行期全局后端（app 的 GpuBootstrap 默认顺序 **wgpu → Metal →
  Vulkan**）。因此"优先 WebGPU"由框架现有默认序直接满足，无需插件侧配置。
- WGSL 为单源主实现（wgpu/WebGPU 路径），MSL/GLSL 为 Metal/Vulkan 对照后端镜像。
- `device` 参数：`auto`（默认，GPU 不可用/失败回落 CPU 并 WARN）/ `gpu`（不可用即 FAILED，
  不静默降级）/ `cpu`。
- 无 GPU 后端的平台（Android、无卡树、CI 软渲染跳过）编译照常、运行期走 CPU——与
  face/matting 的"引擎产物缺失不 return()"同款语义，mobile/WASM STATIC 编入。

### 2.3 为什么不用 `GpuImageTaskBase::run_gpu_op`

run_gpu_op 从 ctx 端口重读输入并**强制通道数等于 op 声明**（3/3），无法表达"任意 1/3/4
通道组合 + 输出布局跟 base"。BlendTask 照 run_gpu_op 的流程**自驱动后端**（render 子模块
直接调 backend API 的同款先例）：任意通道、GPU-resident 输入直通（ensure_gpu 命中链式
gpu_buffer 零拷贝）、输出 GPU-resident。op 的 `pack_uniforms`/`input_channels=3` 回调仅供
`GpuComputeTask` 通用入口（RGB 情形可用）。

### 2.4 uniform 布局（kernel 三源 + CPU 共用契约）

```
u[0]=width  u[1]=height  u[2]=mode  u[3]=float_bits(opacity)
u[4]=in_ch  u[5]=in2_ch  u[6]=out_ch  u[7]=seed
u[8]=flags  bit0=in(BGR) bit1=in2(BGR) bit2=out(BGR)
```

字节访问沿用核心 2in 契约（binding 0=src/1=src2/2=dst、uniform 在 3；3ch 相邻像素共享
u32 word，写用掩码不相交的 atomic And+Or）。`mode` 同时接受字符串标签（"multiply"）与
枚举 int（JSON 两种写法都可）。

### 2.5 Dissolve 的确定性

`dissolve_hash(idx, seed)`：u32 回绕乘异或链（WGSL 与 C++ 的 u32 溢出语义一致），取高
24 位映射 [0,1)（24 位尾数内 f32 转换精确，两侧零舍入差）。同 seed 逐位确定、异 seed
不同——GPU/CPU/重跑三方可复现。Photoshop 的 dissolve 是 dither 阈值有序化，此处用确定性
噪声近似（文档化差异）。

## 3. 双路径一致性与调试记录（浮点坑全集）

两侧（CPU f32 与三份 kernel）**逐语句镜像**（IEEE 运算不满足结合律，`((c−l)×l)/(l−n)`
不能因式化成 `(c−l)×(l/(l−n))`）。即便如此仍踩了四类边界，全部已修并在测试中固化：

1. **fa==1 恒等性**：`b + (s−b)×1` 在 IEEE 下不等于 s（两次舍入）——normal/dissolve@
   opacity1、divide 自除全白等字节级断言全挂。修：fa==1 直通快路径（四处实现同步）。
2. **量化截断 vs round**：见 §1.3。修：四处统一 round-to-nearest。
3. **比较分支的尘埃翻转**：
   - darker/lighter color 的"色版值总和"比较：浮点和的 ulp 尘埃把**相等和**翻成 `<`，
     整像素选错色。修：和比较在**整数字节域**做（byte(v)=round(v×255)，四侧含 double
     参考一致）。
   - hard_mix 的 0.5 阈值：精确有理 0.5（如 `(1−b)/(2s)=1/2` 即 `2(255−base)=src` 的组合）
     在浮点算会低估到 0.4999…。修：阈值带 1e-6 容差（有理网格间距 ~1.5e-5，不会误伤
     真值低于 0.5 的像素；double 参考同样要加，否则参考自身的尘埃也会翻）。
4. **stub 插件陷阱**：测试图经 `opencv_cvt_color` 产灰度 base，而测试进程没加载
   image_color 插件时，DAGSerializer 给未注册类型回退 **stub LambdaNode——COMPLETED 空输出**，
   下游拿到空 any（`type().name()=="v"`）。修：测试 CMake 链接 + Windows 侧显式 dlopen
   image_color（gpu 测试同款 AutoLoadPlugins）。

另记：`g_pixel` 并发初始化 wgpu 的跨进程资源竞争可使 gpu 子模块既有测试在 `ctest -j8`
下偶发 SEGFAULT（单跑全绿，与 blend 无关的既有现象）。

## 4. 测试设计（三层校验）

1. **独立数学参考**：测试驱动内用 double 重写全部 27 模式公式（与实现解耦），CPU 路径
   输出对照（tol 1；整数精确模式 normal/multiply/difference/dissolve@1 为 tol 0）。
2. **双路径对照**：同一图 GPU（全局后端在位，默认 wgpu）与 CPU（全局后端置空 → 任务
   回落）两跑逐字节对照（tol 1；hard_mix 阈值翻转像素按 0.5% 豁免）。
3. **模式性质**：multiply 整数参考 `(2bs+255)/510`、hard_mix 输出二值、divide 自除全白、
   dissolve 同 seed 确定/异 seed 不同/每像素 ∈ {base, blend}、opacity=0.5 半混、RGBA
   alpha 角点语义（blend 层 alpha=0 处颜色不动、alpha 输出 source-over 增长）、灰度 base
   混合布局（1ch×3ch → 1ch）。

资产确定性：`tests/data/gen_blend_src.py`（PIL，入库可复现）生成 hue sweep + value ramp +
黑/白/中灰/三原色色块的 blend_src.png（覆盖 burn/dodge 除零分支与 HSL 路径）及 RGBA
变体；test.png 拷自 gpu 子模块（0/255 全范围）。

ctest：17 张提交图各一条 + `sweep`（27 模式全扫 + dissolve/opacity 专项）。
验证记录（2026-09-20，macOS/arm64）：**wgpu 默认 18/18、Metal 对照 18/18、CPU-only
18/18、GLSL 经 glslangValidator -V 与 glslc（shaderc）双验证、全仓 265 测试无回归**。
Vulkan/MoltenVK 运行时对照未在本机跑（本构建未开 VULKAN；CI Linux 原生覆盖）。

## 5. CI 接入待办（本地仓库 → CI 需要）

`subnode.json` 已注册（`Subnode.cmake` 对缺失 local 目录 WARNING 跳过，CI 不受影响）；
`.gitmodules` 已预留 `plugin-blend` 条目；**主仓库未 force-add gitlink**（远端不存在，
add 了会让 CI 的 `git submodule update --init --recursive` 404 全挂）。接入步骤：

1. `gh repo create mangoeffect/plugin-blend --private` + push `submodules/blend`（main 分支）；
2. 生成第 6 把 ed25519 deploy key（read-only），按 AGENTS.md 轮换流程重设
   `SUBMODULES_DEPLOY_KEYS`（固定顺序 opencv,gpu,render,face,matting,blend——顺序变更是
   破坏性动作，6 把全部重新生成）；
3. `git add -f submodules/blend`（gitlink 过 ignore 规则）。

## 6. 已知边界与后续方向

- 混合层与 base 尺寸必须一致（不一致 FAILED 报错；不做自动对齐/裁切——Photoshop 的画布
  对齐语义属于编排层，可后续用 `render_pipeline` 式图编排表达）。
- `render_pipeline`/`GpuComputeTask` 通用入口只支持 3/3ch RGB 情形（完整路径走 `blend` task）。
- 与 Photoshop 8-bit 管线逐位对齐（每步整数舍入）如需考古级复刻，可在 CPU 参考中加
  per-step round 变体（当前 f32 单次舍入已满足双路径一致 + 独立参考校验）。
- WASM：blend 随 subnode.json 自动进 wasm 构建链（STATIC + whole-archive），浏览器内
  WebGPU 可用即走 GPU 路径（GpuBootstrap 全局后端），否则 CPU 回落——无需进
  `WEBGPU_MODULES` 运行时探测清单（有 CPU 兜底）。Android 同理（纯 CPU）。

## 7. GraphStudio 集成验证（2026-09-20 补记）

- **桌面**：`PluginBootstrap` 启动期扫 `build/submodules/*` 自动加载 blend.dylib；任务库/
  参数面板（enum→combo）/端口（in/in2/out）/GPU 后端（GpuBootstrap 默认 wgpu）全部走框架
  通用机制，零 app 侧接线。app 唯一改动：`GraphViewModel::classifyTask` 增加 "Blend" 分类
  （否则掉进兜底 "Process"）。无头冒烟（`--open --run`）：multiply（可分离/GPU 路径）与
  hue（非分离）均 `3 ok, 0 failed`、无 CPU 回落警告。
- **WASM**：app CMake 的 subnode 解析块自动 add_subdirectory(blend)（STATIC + whole-archive），
  验证：wasm 二进制含 blend 任务名/27 模式标签/WGSL kernel 源串。
- **Android**：三处清单同步（`build_android.py SUBMODULE_TARGETS` /
  `tests/android CMakeLists TG_E2E_SUBMODULES` / `e2e_android/scenarios.py ANDROID_MODULES`）；
  真机（arm64）实跑 **17 张图全部通过**（CPU 路径）。
- **E2E 图集**：`e2e_graph_cases.discover_graphs` 的 rglob 自动发现 blend 的 17 张图，
  macOS/WASM E2E files 场景零改动覆盖。
- **既有环境问题（与 blend 无关，对照实验确认）**：`build_android/mediapipe/install/
  arm64-v8a/lib/libmediapipe_vision_c.a` 是 BSD-ar 长名格式 + Mach-O `.lo` 成员（iOS
  libtool 产物错装 android 路径，2026-09-14 生成）——既有 `build_android_arm64-v8a` 树的
  `tg_e2e_runner` 链接因此失败；blend 的验证经 scratch 树（MEDIAPIPE=OFF）绕开。修复 =
  重跑 `build_mediapipe.py --platform android` 重产该档案。
