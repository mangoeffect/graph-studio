#pragma once

// WgpuGpuBackend 的 pimpl 实现体：wgpu_backend.cpp 与 wgpu_render.cpp 共享。
//
// 版本钉定：wgpu-native v29.0.1.1（scripts/fetch_wgpu.py）。
// v29 webgpu.h 要点（相对老版本的命名，emscripten 旧头需要垫片）：
//   - WGSL 描述：WGPUShaderSourceWGSL{chain, code: WGPUStringView}
//     （旧名 WGPUShaderModuleWGSLDescriptor{code: char*}，emsdk <3.1.4x）；
//   - 拷贝结构：WGPUTexelCopyBufferInfo / WGPUTexelCopyTextureInfo
//     （旧名 WGPUImageCopyBuffer / WGPUImageCopyTexture）；
//   - 异步一律返回 WGPUFuture + CallbackInfo（mode = WaitAnyOnly），
//     用 wgpuInstanceWaitAny 阻塞收割——无 spin/process events。
#include "wgpu_compat.hpp"

#include <task_graph/gpu_image_ops.hpp>

#include <webgpu/webgpu.h>
#if !defined(__EMSCRIPTEN__)
#include <webgpu/wgpu.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <thread>
#include <unordered_set>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace task_graph {

// error scope 收集结果（WGSL 编译错/管线校验错的同步收割——否则只进
// uncaptured 回调，无法定位是哪次创建失败）
struct WgpuErrorScopeResult {
    std::atomic<bool> done{false};
    bool has_error = false;
    std::string message;
};

void wgpu_on_error_scope(WGPUPopErrorScopeStatus status, WGPUErrorType type,
                         WGPUStringView message, void* userdata1, void* userdata2);

struct WgpuGpuBackendImpl {
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;

    // 全入口串行（递归：上传/下载/拷贝路径内部会再进 wait_render_idle）。
    // wgpu 单队列模型，与 Metal @synchronized / Vulkan recursive 同构。
    std::recursive_mutex mutex;

    // 缓存（生命周期与后端一致；句柄不额外持有引用）
    std::unordered_map<std::string, WGPUComputePipeline> kernel_cache;
    std::unordered_map<std::string, WGPURenderPipeline> render_pipeline_cache;
    std::unordered_map<std::string, WGPUSampler> sampler_cache;

    // 立即模式 pass 状态（P1 批处理：连续 pass 共享一个 command encoder，
    // wait_render_idle 统一 finish+submit+等待）
    WGPUCommandEncoder render_cmd = nullptr;
    WGPURenderPassEncoder render_encoder = nullptr;
    bool pass_open = false;

    // 渲染期临时资源（bind group / 每 draw 的 uniform buffer / 纹理 view），
    // 提交完成（wait_render_idle）后统一释放
    std::vector<WGPUBindGroup> pending_bind_groups;
    std::vector<WGPUBuffer> pending_buffers;
    std::vector<WGPUTextureView> pending_views;

    // 固定绑定布局（group0：binding0..3 纹理 / 4 采样器 / 5 uniform，见
    // wgpu_render.cpp 头注释）+ 未用槽位兜底的 4x4 dummy 纹理
    WGPUBindGroupLayout render_bgl = nullptr;
    WGPUPipelineLayout render_layout = nullptr;
    WGPUTexture dummy_texture = nullptr;
    WGPUTextureView dummy_view = nullptr;

    // 纹理句柄 -> 描述（webgpu.h 无法从句柄反查尺寸；create_texture 登记，
    // free_texture 注销）
    std::map<uintptr_t, GpuTextureDesc> texture_descs;

    // 批处理期纹理生命周期保护：任务层可能在 submit 前就析构输入/目标纹理
    // （图节点局部 GpuTexture 随 run_pass 返回销毁）。被在飞批次引用的纹理
    // 先转入 zombie 列表，wait_render_idle 提交完成后真正销毁——否则
    // wgpuQueueSubmit 报 "Texture has been destroyed" 且 panic。
    std::unordered_set<uintptr_t> batch_textures;
    std::vector<WGPUTexture> zombie_textures;

    // ---- 辅助 ----
    //
    // 重要：wgpu-native（v29）未实现 wgpuInstanceWaitAny（Rust 侧 panic
    // "not implemented"）。异步一律用 WGPUCallbackMode_AllowProcessEvents
    // 回调 + wgpuInstanceProcessEvents 轮询收割（回调在本线程 ProcessEvents
    // 调用栈内触发）。各回调签名不同，但状态对象统一为 {atomic done; ...}。

    template <typename State>
    bool spin_until(State& st) {
        while (!st.done.load(std::memory_order_acquire)) {
            wgpuInstanceProcessEvents(instance);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    struct WorkDoneState {
        std::atomic<bool> done{false};
        bool ok = true;
    };

    static void on_work_done(WGPUQueueWorkDoneStatus status, WGPUStringView,
                             void* userdata1, void*) {
        auto* st = static_cast<WorkDoneState*>(userdata1);
        st->ok = status == WGPUQueueWorkDoneStatus_Success;
        st->done.store(true, std::memory_order_release);
    }

    // 阻塞等待队列上全部已提交工作完成（submit 之后的栅栏语义）
    bool wait_queue_idle() {
        if (!queue) return false;
        WorkDoneState st;
        WGPUQueueWorkDoneCallbackInfo cb{};
        cb.mode = WGPUCallbackMode_AllowProcessEvents;
        cb.userdata1 = &st;
        cb.callback = &on_work_done;
        wgpuQueueOnSubmittedWorkDone(queue, cb);
        spin_until(st);
        return st.ok;
    }

    // WGSL shader module（编译失败经 error scope 捕获并打日志，返回 nullptr）
    WGPUShaderModule create_shader_module(const std::string& code, const char* tag);

    // 把 `create()` 包进 Validation error scope：校验错误（如 entry 缺失、
    // 格式不兼容）同步收割并打日志，返回 false = 创建失败。
    template <typename F>
    bool validated(const char* tag, F&& create) {
        wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
        create();
        WgpuErrorScopeResult r;
        WGPUPopErrorScopeCallbackInfo cb{};
        cb.mode = WGPUCallbackMode_AllowProcessEvents;
        cb.userdata1 = &r;
        cb.callback = &wgpu_on_error_scope;
        wgpuDevicePopErrorScope(device, cb);
        spin_until(r);
        if (r.has_error) {
            std::fprintf(stderr, "  [wgpu] validation failed (%s): %s\n", tag,
                         r.message.c_str());
            return false;
        }
        return true;
    }
};

}  // namespace task_graph
