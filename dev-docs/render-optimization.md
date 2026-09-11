# Render 子模块优化完善方案

范围:`submodules/render/render_task`(任务层)+ 核心侧 `src/gpu_backends/{metal_render.mm, vulkan_render.cpp}` + `src/gpu_render_ops.cpp`。
基线:Metal 7/7 测试通过(commit `8b101e2`);Vulkan 仅本地 MoltenVK 验证,主 build 未编译。

分 5 个阶段(P1→P5),每阶段独立可交付、可测试、可提交;P1/P2 是性能主线,P3 是低风险快赢,P4 打通数据路径,P5 是功能扩展。全程不破坏现有立即模式契约与 7 个测试图。

---

## P1: pass 级同步 → 任务/图级 fence(最高收益)

### 现状
- `metal_render.mm:393`:`end_render_pass` → `[cmd commit] + waitUntilCompleted`。
- `vulkan_render.cpp:854`:同位置 `vkQueueWaitIdle`。
- `render_pipeline` N 个 pass 同步 N 次,CPU/GPU 交替空转。

### 方案
后端各引入一个**帧内异步提交**通道,同步点从 pass 挪到"真正需要结果时":

1. **接口扩展**(`gpu_image_ops.hpp` render 虚函数段,默认实现保持同步语义):
   - `virtual bool wait_render_idle()` — 等待所有在飞 render 提交完成。默认空操作(Metal/Vulkan 覆写)。
   - 语义约定:`download_texture` / `copy_texture_to_buffer` / `end_render_pass`(立即模式旧路径)内部隐式先 `wait_render_idle`,保证旧调用方零改动。
2. **Metal**:活动 pass 的 `end_render_pass` 只 `commit`,不 wait;维护 `inFlightCmd_` 计数,`wait_render_idle` 对每个在飞 buffer `waitUntilCompleted`。command buffer 池上限(如 8)防失控。
3. **Vulkan**:`end_render_pass` 只 `vkQueueSubmit` + fence(不 wait);`wait_render_idle` `vkWaitForFences` 全部在飞 fence 后 reset。同时把 `vkQueueWaitIdle` 从 download/copy 路径替换为"只等覆盖该纹理资源的 fence"(精细依赖追踪可后置,首期全量等即可)。
4. **锁协议**:在飞队列的 mutex 并入现有 `renderMutex_`(recursive),不新增锁序。
5. **任务层**(`render_task.cpp`):`RenderPipelineTask::execute` 末尾显式 `wait_render_idle`(输出纹理移交 Image 前保证 GPU 完成,下游 compute 经 `ensure_gpu_buffer` 时也有隐式等待兜底)。

### 风险与对策
- 跨 pass 读写同一纹理的 hazard:pass N 读 pass N-1 写的纹理。**同队列顺序提交天然满足**(Metal command queue / Vulkan 单 queue timeline),无需 barrier;但 Vulkan 侧布局转换(UNDEFINED→SHADER_READ)依赖前序 fence 完成的假设要改为 submit 内 `vkCmdPipelineBarrier` 表达(目前靠 wait idle 隐式保证,这是必须同步改的点,重点测试)。
- 回归:7 图测试 + 新增 `render_pipeline` 8-pass 深链测试图。

### 验收
- 新增 benchmark(见 P3.4):blur_chain 帧时间下降(Metal 上预期 2x 量级,pass 数越多越明显)。
- 7/7 旧测试不回归,Metal + MoltenVK 双后端。

---

## P2: 纹理池 + Vulkan 内存 suballocation

### 2.1 任务内纹理复用(render_task 层,先行)
- `RenderPipelineTask::execute` 维护 `map<(w,h,fmt), vector<GpuTexturePtr>>` 池:pass 结束后,仅被本 pass 独占持有(引用计数唯一)的中间纹理归还池内复用;末 pass 输出与被 `textures["<i>"]` 后续引用的不归还。
- ping-pong 链(gauss_h→v 同尺寸)从 2N 张纹理降为 2 张。
- 识别"独占":`out_tex.use_count() == 1` 且后续 pass 不再引用(specs 预扫描可判)。

### 2.2 Vulkan 后端内存 suballocation
- 现状:一纹理一 `vkAllocateMemory`(`vulkan_render.cpp:158`),`maxMemoryAllocation` 典型 4096,大图链有触顶风险且分配贵。
- 方案:后端内建简易 bump/桶式 suballocator——按内存类型(host-visible 一类、device-local 一类)整块分配(如 64MB),`bindBufferMemory` 式按对齐切子块;纹理销毁进 free-list 复用。不引 VMA(避免新依赖,逻辑可控)。
- `GpuTexture` 析构回调 `free_texture` 不变,后端内部改走子块回收。
- Metal 免改(MTLTexture 由驱动管理)。

### 验收
- 新增测试:16-pass 同尺寸链,断言 Vulkan device memory 分配次数 ≤ 2(可经计数器 log 暴露);纹理池复用率经 TG_LOG 统计。
- blur_chain benchmark 内存峰值对比。

---

## P3: 低风险快赢(可与 P1 并行)

### 3.1 采样器缓存
- `draw_effect_pass`(`render_helpers.hpp:306`)每次 create/free。改为后端按 `(linear, clamp_to_edge)` 组合缓存(2×2 小表,`renderMutex_` 保护);接口不变,`free_sampler` 对缓存句柄变 no-op,后端 shutdown 统一释放。

### 3.2 文件 shader 解析缓存
- `ShaderResolve_fill_sources`(`render_helpers.hpp:180`)每次 execute 读盘 3 次。任务层加 `unordered_map<path, ShaderResolve>` + 文件 mtime 失效;键含 effect 名(库效果路径)与 `_source_dir`。
- 放在 `RenderTaskBase` 内(实例级缓存即可,同任务反复 execute 是主场景)。

### 3.3 Vulkan framebuffer 缓存
- `vulkan_render.cpp:721/858` begin 建 end 毒。改为 `map<(texture, renderPass), VkFramebuffer>` 缓存,纹理销毁时逐出(挂在 `free_texture` 内)。

### 3.4 Benchmark 测试
- 新增 `test_render_bench`(软跳过式,`--tg-bench` 或 env 开启才断言):blur_chain / 16-pass 链,各后端记录帧耗时,输出 P50;阈值只做 smoke(如 16-pass < 500ms @128px)防恶性回归。为 P1/P2 提供前后对比数据。

### 验收
- 3.1-3.3 各自独立 commit,7/7 测试不回归;bench 记录优化前后数据进本文件附录。

---

## P4: GPU 侧 RGB→RGBA 打通(compute→render 零 CPU 往返)

### 方案
- 核心库新增内置 compute op `to_rgba`(镜像现有 `run_gpu_op` 机制):输入 RGBA8/R32F 之外的 GPU buffer,在 GPU 上 swizzle/pad 成 RGBA。
- `input_to_texture`(`render_helpers.hpp:124`)路径改为:GPU-resident 任意布局 → `ensure_gpu_buffer` → `to_rgba` op → `copy_buffer_to_texture`,全程不经 CPU。
- GRAY/BGR/BGRA/RGB 各一个 kernel(MSL+GLSL 双源,放核心库与现有 compute kernel 同处)。
- F32 输入目前 `to_rgba_image` 直接拒绝(`data_type != UINT8` 即 false)——顺带补 F32 支持。

### 验收
- 新测试图:`compute_bgr → render_passthrough`(compute 任务产出 BGR buffer)断言输出正确且**零 CPU 往返**(可用 hook 计数 download 调用)。

---

## P5: 功能扩展(按需排期)

### 5.1 文件 shader 参数化
- 约定:`shader_path` 搭配 `uniform_floats`("名=值" CSV 或按 slot 顺序 CSV)映射进 128B 块;多输入:文件 shader 声明 `// inputs: 2` 注释或 `inputs` param,按 binding 顺序接。

### 5.2 格式扩展
- `GpuTextureFormat` 增 `RGBA8_SRGB`(Metal `MTLPixelFormatRGBA8Unorm_sRGB` / Vulkan `VK_FORMAT_R8G8B8A8_SRGB`),管线缓存键已含格式,天然兼容。

### 5.3 gauss 参数化
- `radius`(1-8)+ 动态生成 tap 权重(二项式系数归一),uniform 布局改为 [texel_size, radius, ...taps];CPU 参考同步实现。修 `render_effects.cpp:237` 的 `{1,1}` fallback(改为任务 FAILED 或 (0,0) 让 shader 黑区可辨)。

### 5.4 blend 精细化
- `GpuRenderPipelineDesc` 增 blend op/src/dst factor 枚举(默认保持现状 additive-over);缓存键扩展。

---

## 横切事项

### CI(随 P1 一起做)
- `tests.yml` 增一个开 `TASK_GRAPH_ENABLE_VULKAN` 的矩阵位(Linux,`libvulkan-dev` 已有;shaderc 由 setup-build-deps 装)——否则 GLSL 分支永远只在本地验证(当前主 build VULKAN=OFF,测试直接 Skip,已实测确认)。
- MoltenVK 本地复验命令固化进脚本:`scripts/run_render_tests.sh`(Metal + 强制 Vulkan 两种模式)。

### 提交策略
- 每阶段主仓 + `submodules/render` 各自 conventional commit;P1 因改核心库接口需同步文档(AGENTS.md 渲染节)。
- 顺序:P3(快赢,先建 bench 基线)→ P1 → P2 → P4 → P5;P3.4 必须最先落地以获得基线数据。

### 明确不做(本期)
- MRT 多 color target、mip 生成、自定义 descriptor set——等真实需求。
- 引入 VMA 等外部依赖。

---

## 附录:benchmark 基线与优化数据(test_render_bench,128×128,Metal,50 次取 P50)

| 场景 | P3 基线(2025-09,commit 8b101e2 后) | P1 后 | P2 后 |
|---|---|---|---|
| pipeline_2pass | 1.545 ms | 1.273 ms | — |
| pipeline_16pass | 5.321 ms | **2.596 ms (2.05x)** | — |
| chain_8nodes | 3.715 ms | 2.786 ms (1.33x) | — |

(128px 图偏小,数据以同步/提交开销为主——正是 P1 要削减的部分;P2 落地后回填。)
