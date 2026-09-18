// WgpuGpuBackend — wgpu-native 统一 GPU 后端：init/shutdown + buffer 段 +
// compute 段（WGSL）。render 段见 wgpu_render.cpp。
//
// 语义对齐（与 Metal/Vulkan 后端一致）：
//   - allocate 的 buffer 固定 usage = STORAGE|COPY_DST|COPY_SRC|UNIFORM：
//     compute 可绑 storage、queue.writeBuffer 可写、可做拷贝源/目标、可绑
//     uniform。下载走 MAP_READ staging + copyBufferToBuffer（WebGPU 不允许
//     直接 map 设备 buffer）。
//   - dispatch：WGSL 约定 entry 名 = kernel 名；bindings 按序绑 binding
//     0..N-1（storage buffer，read/read_write 类型由 WGSL 声明决定，bind
//     group 走 pipeline auto layout + binding 类型未指定推断）；uniform 绑在
//     binding N（临时小 UBO）。@workgroup_size(1,1,1)，grid = 直派
//     workgroup 数（总线程数与 Metal 语义等价）。
//   - 错误捕获：shader/管线创建包 Validation error scope，校验失败打日志并
//     返回失败（见 impl 的 validated()）。
#include <task_graph/gpu_backends/wgpu_backend.hpp>
#include <task_graph/data_types.hpp>
#include "wgpu_backend_impl.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace task_graph {

using wgpu_compat::str_view;

namespace {

[[maybe_unused]] void on_uncaptured_error(WGPUDevice const*, WGPUErrorType type,
                         WGPUStringView message, void*, void*) {
    std::fprintf(stderr, "  [wgpu] uncaptured error (%d): %.*s\n",
                 static_cast<int>(type),
                 message.length > 0 ? static_cast<int>(message.length) : 0,
                 message.data ? message.data : "");
}

}  // namespace

// ---- error scope 回调（impl 头声明，两个 TU 共用）----

void wgpu_on_error_scope(WGPUPopErrorScopeStatus, WGPUErrorType type,
                         WGPUStringView message, void* userdata1, void*) {
    auto* r = static_cast<WgpuErrorScopeResult*>(userdata1);
    if (r->aborted.load(std::memory_order_acquire)) {
        return;  // 迟到回调（等待已超时放弃）
    }
    r->has_error = type != WGPUErrorType_NoError;
    if (r->has_error) {
        r->message.assign(message.data ? message.data : "",
                          message.length > 0 ? message.length : 0);
    }
    r->done.store(true, std::memory_order_release);
}

WGPUShaderModule WgpuGpuBackendImpl::create_shader_module(const std::string& code,
                                                           const char* tag) {
    WGPUShaderSourceWGSL src = wgpu_compat::make_wgsl_source(code);
    WGPUShaderModuleDescriptor d{};
    d.nextInChain = &src.chain;
#ifdef __EMSCRIPTEN__
    d.label = tag;                     // 旧头：label 是 char*
#else
    d.label = str_view(tag);
#endif
    WGPUShaderModule module = nullptr;
    if (!validated(tag, [&] {
            module = wgpuDeviceCreateShaderModule(device, &d);
        })) {
        if (module) wgpuShaderModuleRelease(module);
        return nullptr;
    }
    return module;
}

WgpuGpuBackend::WgpuGpuBackend()
    : impl_(new WgpuGpuBackendImpl()) {
}

WgpuGpuBackend::~WgpuGpuBackend() {
    shutdown();
    delete impl_;
}

bool WgpuGpuBackend::is_available() const {
    return impl_ != nullptr && impl_->device != nullptr;
}

std::string WgpuGpuBackend::get_backend_name() const {
    return "wgpu";
}

// ---- init / shutdown ----

bool WgpuGpuBackend::init() {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (impl_->device) {
        return true;  // 已初始化
    }

    impl_->instance = wgpuCreateInstance(nullptr);
    if (!impl_->instance) {
        return false;
    }

    // 适配器：默认任意后端（macOS→Metal / Windows→D3D12|Vulkan / Linux→
    // Vulkan），TG_WGPU_BACKEND=metal|vulkan|gl|d3d12 可强制。
    WGPURequestAdapterOptions ao{};
#ifndef __EMSCRIPTEN__
    // 旧 emscripten 头的 RequestAdapterOptions 无 backendType（wasm 上
    // 浏览器自选适配器，本就无需强制）
    if (const char* env = std::getenv("TG_WGPU_BACKEND")) {
        const std::string b = env;
        if (b == "metal") ao.backendType = WGPUBackendType_Metal;
        else if (b == "vulkan") ao.backendType = WGPUBackendType_Vulkan;
        else if (b == "gl" || b == "opengl") ao.backendType = WGPUBackendType_OpenGL;
        else if (b == "d3d12") ao.backendType = WGPUBackendType_D3D12;
    }
#endif
    ao.powerPreference = WGPUPowerPreference_HighPerformance;

    struct AdapterState : public WgpuWaitState {
        WGPUAdapter adapter = nullptr;
        WGPURequestAdapterStatus status = WGPURequestAdapterStatus_Success;
    };
    auto adapter_state = std::make_unique<AdapterState>();
    {
        WGPURequestAdapterCallbackInfo cb{};
        cb.mode = WGPUCallbackMode_AllowProcessEvents;
        cb.userdata1 = adapter_state.get();
        cb.callback = [](WGPURequestAdapterStatus st, WGPUAdapter a,
                         WGPUStringView msg, void* u1, void*) {
            auto* s = static_cast<AdapterState*>(u1);
            if (s->aborted.load(std::memory_order_acquire)) {
                return;  // 迟到回调（等待已超时放弃）
            }
            s->adapter = a;
            s->status = st;
            if (st != WGPURequestAdapterStatus_Success) {
                std::fprintf(stderr, "  [wgpu] adapter status=%d msg=%.*s\n",
                             (int)st, (int)msg.length, msg.data ? msg.data : "");
            }
            s->done.store(true, std::memory_order_release);
        };
        wgpu_compat::tg_request_adapter(impl_->instance, &ao, cb);
        if (!impl_->spin_until(adapter_state)) {
            wgpuInstanceRelease(impl_->instance);
            impl_->instance = nullptr;
            return false;
        }
    }
    if (adapter_state->status != WGPURequestAdapterStatus_Success ||
        adapter_state->adapter == nullptr) {
        std::fprintf(stderr, "  [wgpu] no adapter (status=%d)\n",
                     (int)adapter_state->status);
        wgpuInstanceRelease(impl_->instance);
        impl_->instance = nullptr;
        return false;
    }
    WGPUAdapter adapter = adapter_state->adapter;
    impl_->adapter = adapter;

    // 设备：默认 limits/features；uncaptured 错误打到 stderr 便于定位
    //（旧 emscripten 头的 DeviceDescriptor 无该字段——wasm 上 init 本就
    // 优雅失败，回调装不上无妨）
    WGPUDeviceDescriptor dd{};
#ifndef __EMSCRIPTEN__
    WGPUUncapturedErrorCallbackInfo err{};
    err.nextInChain = nullptr;
    err.callback = on_uncaptured_error;
    dd.uncapturedErrorCallbackInfo = err;
#endif

    struct DeviceState : public WgpuWaitState {
        WGPUDevice device = nullptr;
    };
    auto device_state = std::make_unique<DeviceState>();
    {
        WGPURequestDeviceCallbackInfo cb{};
        cb.mode = WGPUCallbackMode_AllowProcessEvents;
        cb.userdata1 = device_state.get();
        cb.callback = [](WGPURequestDeviceStatus st, WGPUDevice d,
                         WGPUStringView msg, void* u1, void*) {
            auto* s = static_cast<DeviceState*>(u1);
            if (s->aborted.load(std::memory_order_acquire)) {
                return;  // 迟到回调（等待已超时放弃）
            }
            s->device = d;
            if (st != WGPURequestDeviceStatus_Success) {
                std::fprintf(stderr, "  [wgpu] device status=%d msg=%.*s\n",
                             (int)st, (int)msg.length, msg.data ? msg.data : "");
            }
            s->done.store(true, std::memory_order_release);
        };
        wgpu_compat::tg_request_device(adapter, &dd, cb);
        if (!impl_->spin_until(device_state)) {
            wgpuAdapterRelease(adapter);
            wgpuInstanceRelease(impl_->instance);
            impl_->adapter = nullptr;
            impl_->instance = nullptr;
            return false;
        }
    }
    WGPUDevice device = device_state->device;
    if (device == nullptr) {
        std::fprintf(stderr, "  [wgpu] requestDevice failed\n");
        wgpuAdapterRelease(adapter);
        wgpuInstanceRelease(impl_->instance);
        impl_->adapter = nullptr;
        impl_->instance = nullptr;
        return false;
    }
    impl_->device = device;
    impl_->queue = wgpuDeviceGetQueue(device);
    return impl_->queue != nullptr;
}

void WgpuGpuBackend::shutdown() {
    if (!impl_) {
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (impl_->render_encoder) {
        wgpuRenderPassEncoderRelease(impl_->render_encoder);
        impl_->render_encoder = nullptr;
    }
    if (impl_->render_cmd) {
        wgpuCommandEncoderRelease(impl_->render_cmd);
        impl_->render_cmd = nullptr;
    }
    for (WGPUBindGroup bg : impl_->pending_bind_groups) wgpuBindGroupRelease(bg);
    for (WGPUBuffer b : impl_->pending_buffers) wgpuBufferRelease(b);
    for (WGPUTextureView v : impl_->pending_views) wgpuTextureViewRelease(v);
    impl_->pending_bind_groups.clear();
    impl_->pending_buffers.clear();
    impl_->pending_views.clear();
    if (impl_->dummy_view) { wgpuTextureViewRelease(impl_->dummy_view); impl_->dummy_view = nullptr; }
    if (impl_->dummy_texture) { wgpuTextureRelease(impl_->dummy_texture); impl_->dummy_texture = nullptr; }
    if (impl_->render_layout) { wgpuPipelineLayoutRelease(impl_->render_layout); impl_->render_layout = nullptr; }
    if (impl_->render_bgl) { wgpuBindGroupLayoutRelease(impl_->render_bgl); impl_->render_bgl = nullptr; }
    for (auto& kv : impl_->kernel_cache) wgpuComputePipelineRelease(kv.second);
    impl_->kernel_cache.clear();
    for (auto& kv : impl_->render_pipeline_cache) wgpuRenderPipelineRelease(kv.second);
    impl_->render_pipeline_cache.clear();
    for (auto& kv : impl_->sampler_cache) wgpuSamplerRelease(kv.second);
    impl_->sampler_cache.clear();
    if (impl_->queue) { wgpuQueueRelease(impl_->queue); impl_->queue = nullptr; }
    if (impl_->device) { wgpuDeviceRelease(impl_->device); impl_->device = nullptr; }
    if (impl_->adapter) { wgpuAdapterRelease(impl_->adapter); impl_->adapter = nullptr; }
    if (impl_->instance) { wgpuInstanceRelease(impl_->instance); impl_->instance = nullptr; }
}

// ---- buffer 段 ----

uintptr_t WgpuGpuBackend::allocate_gpu_memory(size_t size) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (!is_available() || size == 0) {
        return 0;
    }
    WGPUBufferDescriptor d{};
    d.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst |
              WGPUBufferUsage_CopySrc | WGPUBufferUsage_Uniform;
    // WGSL 侧 storage 是 array<u32> 视角：绑定范围必须 4 字节对齐（w*h*3 类
    // 尺寸不是 4 的倍数会触发 bind group 校验 panic）。补零对齐不影响逻辑
    // 大小——download/upload 都按 Image 的 total_size() 拷贝。
    d.size = (size + 3) & ~size_t(3);
    d.mappedAtCreation = false;
    WGPUBuffer buf = wgpuDeviceCreateBuffer(impl_->device, &d);
    return buf ? reinterpret_cast<uintptr_t>(buf) : 0;
}

void WgpuGpuBackend::free_gpu_memory(uintptr_t handle) {
    if (handle == 0) {
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    WGPUBuffer buf = reinterpret_cast<WGPUBuffer>(handle);
    wgpuBufferDestroy(buf);
    wgpuBufferRelease(buf);
}

bool WgpuGpuBackend::upload_to_gpu(Image& image) {
    if (!image.is_on_cpu() || !is_available()) {
        return false;
    }
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    const size_t total = image.total_size();
    uintptr_t handle = allocate_gpu_memory(total);
    if (handle == 0) {
        return false;
    }
    wgpuQueueWriteBuffer(impl_->queue,
                         reinterpret_cast<WGPUBuffer>(handle), 0,
                         image.ptr(), total);
    image.gpu_handle = handle;
    return true;
}

bool WgpuGpuBackend::download_to_cpu(Image& image) {
    if (!impl_->entry_allowed("download_to_cpu")) {
        return false;
    }
    if (!image.is_on_gpu() || !is_available()) {
        return false;
    }
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    WGPUBuffer src = reinterpret_cast<WGPUBuffer>(image.gpu_handle);
    if (!src) {
        return false;
    }
    const size_t total = image.total_size();
    if (!image.data) {
        image.data = std::make_shared<std::vector<uint8_t>>(total);
    }
    if (image.data->size() != total) {
        image.data->resize(total);
    }

    // MAP_READ staging + copyBufferToBuffer + map（WebGPU 不能直 map 设备 buffer）。
    // 拷贝尺寸须 4 字节对齐（COPY_BUFFER_ALIGNMENT）：按补齐后的尺寸拷/
    // map，主机侧只 memcpy 逻辑 total（尾部 padding 丢弃）。
    const size_t copy_size = (total + 3) & ~size_t(3);
    WGPUBufferDescriptor d{};
    d.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    d.size = copy_size;
    WGPUBuffer staging = wgpuDeviceCreateBuffer(impl_->device, &d);
    if (!staging) {
        return false;
    }

    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(impl_->device, nullptr);
    wgpuCommandEncoderCopyBufferToBuffer(enc, src, 0, staging, 0, copy_size);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(impl_->queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(enc);

    struct MapState : public WgpuWaitState {
        bool ok = false;
    };
    auto map_state = std::make_unique<MapState>();
    {
        WGPUBufferMapCallbackInfo cb{};
        cb.mode = WGPUCallbackMode_AllowProcessEvents;
        cb.userdata1 = map_state.get();
        cb.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* u1, void*) {
            auto* s = static_cast<MapState*>(u1);
            if (s->aborted.load(std::memory_order_acquire)) {
                return;  // 迟到回调（等待已超时放弃）
            }
            s->ok = status == WGPUMapAsyncStatus_Success;
            s->done.store(true, std::memory_order_release);
        };
        wgpu_compat::tg_buffer_map_async(staging, WGPUMapMode_Read, 0, copy_size, cb);
        if (!impl_->spin_until(map_state)) {
            wgpuBufferDestroy(staging);
            wgpuBufferRelease(staging);
            return false;
        }
    }
    bool ok = map_state->ok;
    if (ok) {
        const void* mapped = wgpuBufferGetConstMappedRange(staging, 0, copy_size);
        if (mapped) {
            std::memcpy(image.ptr(), mapped, total);
        } else {
            ok = false;
        }
        wgpuBufferUnmap(staging);
    }
    wgpuBufferDestroy(staging);
    wgpuBufferRelease(staging);
    return ok;
}

bool WgpuGpuBackend::copy_to_gpu(Image& image, uintptr_t gpu_ptr) {
    if (!image.is_on_cpu() || gpu_ptr == 0 || !is_available()) {
        return false;
    }
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    wgpuQueueWriteBuffer(impl_->queue, reinterpret_cast<WGPUBuffer>(gpu_ptr), 0,
                         image.ptr(), image.total_size());
    image.gpu_handle = gpu_ptr;
    return true;
}

bool WgpuGpuBackend::release_gpu_memory(Image& image) {
    if (!image.is_on_gpu()) {
        return false;
    }
    if (image.gpu_buffer) {
        image.gpu_buffer.reset();
    } else if (image.gpu_handle != 0) {
        free_gpu_memory(image.gpu_handle);
    }
    image.gpu_handle = 0;
    return true;
}

// ---- compute 段（WGSL）----

uintptr_t WgpuGpuBackend::compile_kernel(const std::string& name,
                                          const std::string& source) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (!is_available() || !impl_->entry_allowed("compile_kernel")) {
        return 0;
    }
    auto it = impl_->kernel_cache.find(name);
    if (it != impl_->kernel_cache.end()) {
        return reinterpret_cast<uintptr_t>(it->second);
    }

    WGPUShaderModule module = impl_->create_shader_module(source, name.c_str());
    if (!module) {
        return 0;
    }

    WGPUComputePipelineDescriptor d{};
    d.compute.module = module;  // 约定：entry 名 = kernel 名；layout = NULL
                                // → auto layout（见头注释 dispatch 约定）
#ifdef __EMSCRIPTEN__
    d.compute.entryPoint = name.c_str();  // 旧头：compute 内联且 entry 是 char*
#else
    WGPUComputeState cs{};
    cs.module = module;
    cs.entryPoint = str_view(name);
    d.compute = cs;
#endif
    WGPUComputePipeline pipe = nullptr;
    const bool ok = impl_->validated(name.c_str(), [&] {
        pipe = wgpuDeviceCreateComputePipeline(impl_->device, &d);
    });
    wgpuShaderModuleRelease(module);
    if (!ok || !pipe) {
        if (pipe) wgpuComputePipelineRelease(pipe);
        return 0;
    }
    impl_->kernel_cache[name] = pipe;
    return reinterpret_cast<uintptr_t>(pipe);
}

bool WgpuGpuBackend::dispatch(uintptr_t kernel,
                               const std::vector<GpuBinding>& bindings,
                               const void* uniform_data, size_t uniform_size,
                               uint32_t grid_x, uint32_t grid_y, uint32_t grid_z) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (!is_available() || kernel == 0 || !impl_->entry_allowed("dispatch")) {
        return false;
    }
    WGPUComputePipeline pipe = reinterpret_cast<WGPUComputePipeline>(kernel);

    // 临时 uniform（小 UBO + writeBuffer）
    WGPUBuffer uniform_buf = nullptr;
    if (uniform_data && uniform_size > 0) {
        WGPUBufferDescriptor ud{};
        ud.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        ud.size = (uniform_size + 3) & ~size_t(3);
        uniform_buf = wgpuDeviceCreateBuffer(impl_->device, &ud);
        if (!uniform_buf) {
            return false;
        }
        wgpuQueueWriteBuffer(impl_->queue, uniform_buf, 0, uniform_data,
                             uniform_size);
    }

    // bind group：auto layout（GetBindGroupLayout(0)），buffer 类型不指定推断
    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipe, 0);
    std::vector<WGPUBindGroupEntry> entries;
    entries.reserve(bindings.size() + 1);
    for (size_t i = 0; i < bindings.size(); ++i) {
        WGPUBindGroupEntry e{};
        e.binding = static_cast<uint32_t>(i);
        e.buffer = reinterpret_cast<WGPUBuffer>(bindings[i].handle);
        e.offset = bindings[i].offset;
        e.size = WGPU_WHOLE_SIZE;
        entries.push_back(e);
    }
    if (uniform_buf) {
        WGPUBindGroupEntry e{};
        e.binding = static_cast<uint32_t>(bindings.size());  // 约定：uniform 在末位
        e.buffer = uniform_buf;
        e.offset = 0;
        e.size = uniform_size;
        entries.push_back(e);
    }
    WGPUBindGroupDescriptor bd{};
    bd.layout = bgl;
    bd.entryCount = entries.size();
    bd.entries = entries.data();
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(impl_->device, &bd);
    wgpuBindGroupLayoutRelease(bgl);
    if (!bg) {
        if (uniform_buf) { wgpuBufferDestroy(uniform_buf); wgpuBufferRelease(uniform_buf); }
        return false;
    }

    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(impl_->device, nullptr);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(enc, nullptr);
    wgpuComputePassEncoderSetPipeline(pass, pipe);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, grid_x, grid_y, grid_z);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(impl_->queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(enc);

    // 同步语义与 Metal waitUntilCompleted / Vulkan fence 一致：dispatch 返回即完成
    bool ok = impl_->wait_queue_idle();
    wgpuBindGroupRelease(bg);
    if (uniform_buf) { wgpuBufferDestroy(uniform_buf); wgpuBufferRelease(uniform_buf); }
    return ok;
}

void WgpuGpuBackend::release_kernel(uintptr_t kernel) {
    // 管线由 kernel_cache 持有，随 shutdown 释放（与 Metal 一致）
    (void)kernel;
}

bool WgpuGpuBackend::supports_compute() const {
    return is_available();
}

std::string WgpuGpuBackend::kernel_language() const {
    return "wgsl";
}

}  // namespace task_graph
