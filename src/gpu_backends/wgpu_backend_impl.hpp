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
#else
#include <emscripten.h>
#include <emscripten/threading.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <thread>
#include <unordered_set>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace task_graph {

// 异步等待状态基类：实例一律堆分配（unique_ptr 持有）+ aborted 旗。
// spin_until 超时放弃后，迟到回调先查 aborted 再写——等待方的栈帧已
// 不可触碰（此前 error scope 的结果对象是 validated() 栈变量，超时路径
// 会悬垂）。超时实例转入 impl 的 abandoned_waits，活到后端析构统一回收
// （回调永不触达的极端情形按泄漏兜底，受超时次数约束）。
struct WgpuWaitState {
    std::atomic<bool> done{false};
    std::atomic<bool> aborted{false};
    virtual ~WgpuWaitState() = default;
};

// error scope 收集结果（WGSL 编译错/管线校验错的同步收割——否则只进
// uncaptured 回调，无法定位是哪次创建失败）
struct WgpuErrorScopeResult : public WgpuWaitState {
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
    // 调用栈内触发）。各回调签名不同，但状态对象统一继承 WgpuWaitState。
    //
    // 超时（默认 10s）是防呆闸门：正常回调毫秒级；超时后 st 进
    // abandoned_waits、迟到的回调只查 aborted 不落写。

    // 超时后的废弃等待（见 WgpuWaitState 注释）
    std::vector<std::unique_ptr<WgpuWaitState>> abandoned_waits;

    // wasm 专属：emscripten_sleep（Asyncify unwind）窗口标志。窗口内浏览器
    // 事件循环运转，可能在等待期间再次进入我方公开入口（典型：图执行期间
    // 用户在结果下拉选中触发懒下载）。Asyncify 单 rewind 缓冲不支持嵌套
    // 挂起——公开入口经 entry_allowed 谢绝窗口期的新调用。native 恒放行。
    bool in_async_wait_ = false;

    // 公开入口的窗口期检查（wasm 上生效；native 恒放行）
    bool entry_allowed(const char* tag) {
        if (!in_async_wait_) {
            return true;
        }
        std::fprintf(stderr,
                     "  [wgpu] GPU call (%s) arrived inside an async wait"
                     " window; declining\n",
                     tag);
        return false;
    }

    template <typename State>
    bool spin_until(std::unique_ptr<State>& st, int timeout_ms = 10000) {
#ifdef __EMSCRIPTEN__
        // 浏览器的异步回调需要 JS 栈解开（事件循环运转）才能送达：
        // emscripten_sleep 借 Asyncify 展开主线程调用栈，回调就在这个
        // 窗口到达（spike 实测 adapter/device/map 均 1-2ms）。worker
        // 线程的事件循环被线程入口长期占用、回调无法送达——本函数只允许
        // 主线程进入（executor 对 GPU 任务在主线程内联派发；此处的线程
        // 检查以响亮失败兜底）。
        if (!wgpu_compat::tg_assert_main_thread()) {
            return false;
        }
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(timeout_ms);
        while (!st->done.load(std::memory_order_acquire)) {
            if (std::chrono::steady_clock::now() >= deadline) {
                st->aborted.store(true, std::memory_order_release);
                abandoned_waits.push_back(std::move(st));
                std::fprintf(stderr, "  [wgpu] async wait timeout (%d ms)\n",
                             timeout_ms);
                return false;
            }
            in_async_wait_ = true;
            emscripten_sleep(1);
            in_async_wait_ = false;
        }
        return true;
#else
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(timeout_ms);
        while (!st->done.load(std::memory_order_acquire)) {
            if (std::chrono::steady_clock::now() >= deadline) {
                st->aborted.store(true, std::memory_order_release);
                abandoned_waits.push_back(std::move(st));
                std::fprintf(stderr, "  [wgpu] async wait timeout (%d ms)\n",
                             timeout_ms);
                return false;
            }
            wgpuInstanceProcessEvents(instance);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
#endif
    }

    struct WorkDoneState : public WgpuWaitState {
        bool ok = true;
    };

    static void on_work_done(WGPUQueueWorkDoneStatus status, WGPUStringView,
                             void* userdata1, void*) {
        auto* st = static_cast<WorkDoneState*>(userdata1);
        if (st->aborted.load(std::memory_order_acquire)) {
            return;  // 迟到回调（等待已超时放弃）
        }
        st->ok = status == WGPUQueueWorkDoneStatus_Success;
        st->done.store(true, std::memory_order_release);
    }

    // 阻塞等待队列上全部已提交工作完成（submit 之后的栅栏语义）
    bool wait_queue_idle() {
        if (!queue) return false;
        auto st = std::make_unique<WorkDoneState>();
        WGPUQueueWorkDoneCallbackInfo cb{};
        cb.mode = WGPUCallbackMode_AllowProcessEvents;
        cb.userdata1 = st.get();
        cb.callback = &on_work_done;
        wgpu_compat::tg_queue_work_done(queue, cb);
        if (!spin_until(st)) return false;
        return st->ok;
    }

    // WGSL shader module（编译失败经 error scope 捕获并打日志，返回 nullptr）
    WGPUShaderModule create_shader_module(const std::string& code, const char* tag);

    // 把 `create()` 包进 Validation error scope：校验错误（如 entry 缺失、
    // 格式不兼容）同步收割并打日志，返回 false = 创建失败。
    template <typename F>
    bool validated(const char* tag, F&& create) {
        wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
        create();
        auto r = std::make_unique<WgpuErrorScopeResult>();
        WGPUPopErrorScopeCallbackInfo cb{};
        cb.mode = WGPUCallbackMode_AllowProcessEvents;
        cb.userdata1 = r.get();
        cb.callback = &wgpu_on_error_scope;
        wgpu_compat::tg_pop_error_scope(device, cb);
        if (!spin_until(r)) return false;
        if (r->has_error) {
            std::fprintf(stderr, "  [wgpu] validation failed (%s): %s\n", tag,
                         r->message.c_str());
            return false;
        }
        return true;
    }
};

}  // namespace task_graph
