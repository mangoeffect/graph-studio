#include "view/WgpuImageViewer.h"

#include <QGuiApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

#ifdef TG_APP_HAS_WGPU
#include <webgpu/webgpu.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(TG_VIEWER_X11)
#include <X11/Xlib.h>
#endif
#endif  // TG_APP_HAS_WGPU

using namespace graph_studio;

#ifdef TG_APP_HAS_WGPU

// 字面量/静态串 → WGPUStringView（不接临时 std::string：c_str 会悬垂）
static WGPUStringView sv(const char* s) {
    WGPUStringView v;
    v.data = s;
    v.length = std::char_traits<char>::length(s);
    return v;
}

// ---------------------------------------------------------------------------
// WGSL blit：全屏大三角形，quad varying 即 NDC 坐标（wgpu Metal 风格：
// y=+1 是帧缓冲顶行）。uniform = (scale.xy, pan.zw)：NDC → 图像归一化坐标
// q = (quad - pan) / scale，q.y=+1 对应图像上沿；uv 从左上角起。
// ---------------------------------------------------------------------------
static const char* kBlitWgsl = R"WGSL(
struct Uni { sp: vec4<f32> };
@group(0) @binding(0) var<uniform> uni: Uni;
@group(0) @binding(1) var smp: sampler;
@group(0) @binding(2) var tex: texture_2d<f32>;

struct VsOut {
    @builtin(position) pos: vec4<f32>,
    @location(0) quad: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) i: u32) -> VsOut {
    var p = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0),
                                vec2<f32>( 3.0, -1.0),
                                vec2<f32>(-1.0,  3.0));
    var out: VsOut;
    out.pos = vec4<f32>(p[i], 0.0, 1.0);
    out.quad = p[i];
    return out;
}

@fragment
fn fs_main(in: VsOut) -> @location(0) vec4<f32> {
    let q = (in.quad - uni.sp.zw) / uni.sp.xy;
    if (q.x < -1.0 || q.x > 1.0 || q.y < -1.0 || q.y > 1.0) {
        discard;
    }
    let uv = vec2<f32>((q.x + 1.0) * 0.5, (1.0 - q.y) * 0.5);
    return textureSample(tex, smp, uv);
}
)WGSL";

struct WgpuImageViewer::Impl {
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUSurface surface = nullptr;

    WGPURenderPipeline pipeline = nullptr;
    WGPUSampler sampler = nullptr;
    WGPUBuffer uniform_buf = nullptr;  // 16B (vec4)

    WGPUTexture image_texture = nullptr;
    WGPUTextureView image_view = nullptr;

    // "No image" 占位纹理（CPU 光栅化文字 → 上传）
    WGPUTexture placeholder_texture = nullptr;
    WGPUTextureView placeholder_view = nullptr;

    // surface 配置状态（尺寸变化时 reconfigure）
    WGPUTextureFormat surface_format = WGPUTextureFormat_Undefined;

#if defined(__APPLE__)
    CAMetalLayer* metal_layer = nullptr;  // 挂在 widget 的 NSView 上（自持一份）
#endif

    // ---- 异步收割（与核心库 wgpu 后端同款：v29 wgpuInstanceWaitAny 未实现，
    // 回调 AllowProcessEvents + ProcessEvents 轮询） ----
    template <typename State>
    static void spin_until(WGPUInstance instance, State& st) {
        while (!st.done.load(std::memory_order_acquire)) {
            wgpuInstanceProcessEvents(instance);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    static void on_uncaptured_error(WGPUDevice const*, WGPUErrorType type,
                                    WGPUStringView msg, void*, void*) {
        std::fprintf(stderr, "  [wgpu-viewer] uncaptured error (type=%d): %.*s\n",
                     (int)type, (int)msg.length, msg.data ? msg.data : "");
    }

    void releaseImageTexture() {
        if (image_view) { wgpuTextureViewRelease(image_view); image_view = nullptr; }
        if (image_texture) { wgpuTextureRelease(image_texture); image_texture = nullptr; }
    }

    // QImage（RGBA8888）→ 纹理。writeTexture 的 bytesPerRow 按 256 对齐
    // （规范要求；未对齐宽度时垫行拷贝）。
    bool uploadImage(const QImage& img, WGPUTexture* out_tex, WGPUTextureView* out_view) {
        QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
        const uint32_t w = rgba.width(), h = rgba.height();
        const uint32_t bpr = w * 4;
        const uint32_t aligned = (bpr + 255u) & ~255u;
        std::vector<uint8_t> host;
        const uint8_t* src = rgba.constBits();
        if (aligned != bpr) {
            host.resize((size_t)aligned * h, 0);
            for (uint32_t y = 0; y < h; ++y) {
                std::memcpy(host.data() + (size_t)y * aligned,
                            src + (size_t)y * bpr, bpr);
            }
            src = host.data();
        }
        WGPUTextureDescriptor td{};
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.size = {w, h, 1};
        td.format = WGPUTextureFormat_RGBA8Unorm;
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        WGPUTexture tex = wgpuDeviceCreateTexture(device, &td);
        if (!tex) return false;

        WGPUTexelCopyTextureInfo dst{};
        dst.texture = tex;
        dst.origin = {0, 0, 0};
        dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.offset = 0;
        layout.bytesPerRow = aligned;
        layout.rowsPerImage = h;
        WGPUExtent3D extent = {w, h, 1};
        wgpuQueueWriteTexture(queue, &dst, src, (size_t)aligned * h, &layout, &extent);

        WGPUTextureViewDescriptor vd{};
        vd.format = WGPUTextureFormat_RGBA8Unorm;
        vd.dimension = WGPUTextureViewDimension_2D;
        vd.baseMipLevel = 0;
        vd.mipLevelCount = 1;
        vd.baseArrayLayer = 0;
        vd.arrayLayerCount = 1;
        vd.aspect = WGPUTextureAspect_All;
        WGPUTextureView view = wgpuTextureCreateView(tex, &vd);
        if (!view) {
            wgpuTextureRelease(tex);
            return false;
        }
        *out_tex = tex;
        *out_view = view;
        return true;
    }

    // 平台原生 surface。失败返回 nullptr（→ QLabel 降级）。
    static WGPUSurface create_platform_surface(WGPUInstance instance,
                                               WgpuImageViewer* w, Impl* impl) {
#if defined(__APPLE__)
        // NSView 换成自持 CAMetalLayer（layer-hosting 子视图渲染在窗口 backing
        // store 之上——Qt+Metal 嵌入的标准做法）。offscreen（UI 测试）等非
        // cocoa 平台的 winId 不是 NSView——直接放弃，走 QLabel 降级。
        if (QGuiApplication::platformName() != QStringLiteral("cocoa")) return nullptr;
        NSView* view = reinterpret_cast<NSView*>(w->winId());
        if (!view) return nullptr;
        CAMetalLayer* layer = [CAMetalLayer new];
        if (!layer) return nullptr;
        layer.contentsScale = w->devicePixelRatioF();
        view.wantsLayer = YES;
        view.layer = layer;
        impl->metal_layer = layer;

        WGPUSurfaceSourceMetalLayer src{};
        src.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
        src.layer = layer;
        WGPUSurfaceDescriptor desc{};
        desc.nextInChain = &src.chain;
        return wgpuInstanceCreateSurface(instance, &desc);
#elif defined(_WIN32)
        HWND hwnd = reinterpret_cast<HWND>(w->winId());
        if (!hwnd) return nullptr;
        WGPUSurfaceSourceWindowsHWND src{};
        src.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
        src.hinstance = GetModuleHandle(nullptr);
        src.hwnd = hwnd;
        WGPUSurfaceDescriptor desc{};
        desc.nextInChain = &src.chain;
        return wgpuInstanceCreateSurface(instance, &desc);
#elif defined(TG_VIEWER_X11)
        // X11/XWayland（xcb 平台）。独立 X 连接拿 Display*——窗口 ID 服务器
        // 全局，跨连接引用合法（wgpu 仅用其查询 visual）。Wayland 原生会话
        // 无公开 wl_surface 句柄，返回 null 走 QLabel 降级。
        if (QGuiApplication::platformName() != QStringLiteral("xcb")) return nullptr;
        Display* display = XOpenDisplay(nullptr);
        if (!display) return nullptr;
        WGPUSurfaceSourceXlibWindow src{};
        src.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
        src.display = display;
        src.window = static_cast<uint64_t>(w->winId());
        WGPUSurfaceDescriptor desc{};
        desc.nextInChain = &src.chain;
        return wgpuInstanceCreateSurface(instance, &desc);
#else
        (void)instance;
        (void)w;
        (void)impl;
        return nullptr;
#endif
    }

    void destroy() {
        releaseImageTexture();
        if (placeholder_view) { wgpuTextureViewRelease(placeholder_view); placeholder_view = nullptr; }
        if (placeholder_texture) { wgpuTextureRelease(placeholder_texture); placeholder_texture = nullptr; }
        if (pipeline) { wgpuRenderPipelineRelease(pipeline); pipeline = nullptr; }
        if (sampler) { wgpuSamplerRelease(sampler); sampler = nullptr; }
        if (uniform_buf) { wgpuBufferDestroy(uniform_buf); wgpuBufferRelease(uniform_buf); uniform_buf = nullptr; }
        if (surface) {
            wgpuSurfaceUnconfigure(surface);
            wgpuSurfaceRelease(surface);
            surface = nullptr;
        }
        if (device) { wgpuDeviceRelease(device); device = nullptr; }
        if (adapter) { wgpuAdapterRelease(adapter); adapter = nullptr; }
        if (instance) { wgpuInstanceRelease(instance); instance = nullptr; }
#if defined(__APPLE__)
        if (metal_layer) {
            CFRelease(metal_layer);
            metal_layer = nullptr;
        }
#endif
    }
};

#else  // !TG_APP_HAS_WGPU

struct WgpuImageViewer::Impl {};

#endif  // TG_APP_HAS_WGPU

// ---------------------------------------------------------------------------
// widget
// ---------------------------------------------------------------------------

WgpuImageViewer::WgpuImageViewer(QWidget* parent)
    : QWidget(parent)
#ifdef TG_APP_HAS_WGPU
    , impl_(new Impl())
#endif
{
    setMouseTracking(true);
    setMinimumSize(200, 150);
}

WgpuImageViewer::~WgpuImageViewer() {
#ifdef TG_APP_HAS_WGPU
    if (impl_) {
        impl_->destroy();
        delete impl_;
    }
#endif
}

void WgpuImageViewer::quadScale(float& sx, float& sy) const {
    // Fit-to-view（contain）：zoom=1 时图像在保持纵横比下尽量填满视口。
    // 与旧 GL 查看器同一公式（screenToImage/clampPan/wheelEvent 共用）。
    const float vw = static_cast<float>(width());
    const float vh = static_cast<float>(height());
    const float iw = static_cast<float>(image_.width());
    const float ih = static_cast<float>(image_.height());
    if (iw <= 0 || ih <= 0 || vw <= 0 || vh <= 0) {
        sx = sy = 1.0f;
        return;
    }
    const float imageAspect = iw / ih;
    const float viewportAspect = vw / vh;
    if (imageAspect > viewportAspect) {
        sx = zoom_;
        sy = (viewportAspect / imageAspect) * zoom_;
    } else {
        sx = (imageAspect / viewportAspect) * zoom_;
        sy = zoom_;
    }
}

void WgpuImageViewer::setImage(const QImage& image) {
    image_ = image;
    resetView();
#ifdef TG_APP_HAS_WGPU
    if (!wgpuFailed_ && impl_ && impl_->device && !image_.isNull()) {
        impl_->releaseImageTexture();
        if (!impl_->uploadImage(image_, &impl_->image_texture, &impl_->image_view)) {
            // 上传失败（多为设备丢失/超限）→ 降级
            enterFallbackMode();
        }
    }
#endif
    update();
}

void WgpuImageViewer::clearImage() {
    image_ = QImage();
#ifdef TG_APP_HAS_WGPU
    if (impl_) impl_->releaseImageTexture();
#endif
    update();
}

void WgpuImageViewer::resetView() {
    zoom_ = 1.0f;
    panX_ = 0.0f;
    panY_ = 0.0f;
    update();
}

void WgpuImageViewer::enterFallbackMode() {
    if (wgpuFailed_) return;
    wgpuFailed_ = true;
#ifdef TG_APP_HAS_WGPU
    if (impl_) {
        // 释放 wgpu 资源（surface 尤其要 unconfigure；macOS layer 留在
        // NSView 上无妨——widget 即将只显示 QLabel 子控件）
        impl_->destroy();
    }
#endif
    if (!fallback_) {
        fallback_ = new QLabel(this);
        fallback_->setAlignment(Qt::AlignCenter);
        fallback_->setBackgroundRole(QPalette::Dark);
    }
    updateFallback();
    fallback_->show();
    fallback_->raise();
}

void WgpuImageViewer::updateFallback() {
    if (!fallback_) return;
    if (image_.isNull()) {
        fallback_->setText(QStringLiteral("No image"));
        fallback_->setPixmap(QPixmap());
    } else {
        fallback_->setGeometry(rect());
        fallback_->setPixmap(QPixmap::fromImage(image_).scaled(
            fallback_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

bool WgpuImageViewer::ensureWgpu() {
#ifdef TG_APP_HAS_WGPU
    if (wgpuFailed_ || !impl_) return false;
    if (impl_->device) return true;

    impl_->instance = wgpuCreateInstance(nullptr);
    if (!impl_->instance) { enterFallbackMode(); return false; }

    WGPUSurface surface = Impl::create_platform_surface(impl_->instance, this, impl_);
    if (!surface) {
        std::fprintf(stderr, "  [wgpu-viewer] platform surface unavailable; "
                             "falling back to QLabel view\n");
        wgpuInstanceRelease(impl_->instance);
        impl_->instance = nullptr;
        enterFallbackMode();
        return false;
    }
    impl_->surface = surface;

    // adapter：带上 compatibleSurface（保证可 present）
    struct AdapterState {
        std::atomic<bool> done{false};
        WGPUAdapter adapter = nullptr;
    } st;
    {
        WGPURequestAdapterOptions ao{};
        ao.powerPreference = WGPUPowerPreference_HighPerformance;
        ao.compatibleSurface = surface;
        WGPURequestAdapterCallbackInfo cb{};
        cb.mode = WGPUCallbackMode_AllowProcessEvents;
        cb.userdata1 = &st;
        cb.callback = [](WGPURequestAdapterStatus status, WGPUAdapter a,
                         WGPUStringView msg, void* u1, void*) {
            auto* s = static_cast<AdapterState*>(u1);
            s->adapter = (status == WGPURequestAdapterStatus_Success) ? a : nullptr;
            if (!s->adapter) {
                std::fprintf(stderr, "  [wgpu-viewer] adapter status=%d msg=%.*s\n",
                             (int)status, (int)msg.length, msg.data ? msg.data : "");
            }
            s->done.store(true, std::memory_order_release);
        };
        wgpuInstanceRequestAdapter(impl_->instance, &ao, cb);
        Impl::spin_until(impl_->instance, st);
    }
    if (!st.adapter) {
        enterFallbackMode();
        return false;
    }
    impl_->adapter = st.adapter;

    // device（uncaptured 错误打到 stderr）
    struct DeviceState {
        std::atomic<bool> done{false};
        WGPUDevice device = nullptr;
    } ds;
    {
        WGPUUncapturedErrorCallbackInfo err{};
        err.nextInChain = nullptr;
        err.callback = &Impl::on_uncaptured_error;
        WGPUDeviceDescriptor dd{};
        dd.uncapturedErrorCallbackInfo = err;
        WGPURequestDeviceCallbackInfo cb{};
        cb.mode = WGPUCallbackMode_AllowProcessEvents;
        cb.userdata1 = &ds;
        cb.callback = [](WGPURequestDeviceStatus status, WGPUDevice d,
                         WGPUStringView msg, void* u1, void*) {
            auto* s = static_cast<DeviceState*>(u1);
            s->device = (status == WGPURequestDeviceStatus_Success) ? d : nullptr;
            if (!s->device) {
                std::fprintf(stderr, "  [wgpu-viewer] device status=%d msg=%.*s\n",
                             (int)status, (int)msg.length, msg.data ? msg.data : "");
            }
            s->done.store(true, std::memory_order_release);
        };
        wgpuAdapterRequestDevice(impl_->adapter, &dd, cb);
        Impl::spin_until(impl_->instance, ds);
    }
    if (!ds.device) {
        enterFallbackMode();
        return false;
    }
    impl_->device = ds.device;
    impl_->queue = wgpuDeviceGetQueue(impl_->device);
    if (!impl_->queue) {
        enterFallbackMode();
        return false;
    }

    // surface 首选格式（v29：无 GetPreferredFormat，caps.formats[0] 即首选）
    WGPUSurfaceCapabilities caps{};
    if (wgpuSurfaceGetCapabilities(surface, impl_->adapter, &caps) != WGPUStatus_Success ||
        caps.formatCount == 0) {
        wgpuSurfaceCapabilitiesFreeMembers(caps);
        enterFallbackMode();
        return false;
    }
    impl_->surface_format = caps.formats[0];
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    // sampler + blit 管线（auto layout；颜色格式 = surface 首选格式）
    WGPUSamplerDescriptor sd{};
    sd.addressModeU = WGPUAddressMode_ClampToEdge;
    sd.addressModeV = WGPUAddressMode_ClampToEdge;
    sd.addressModeW = WGPUAddressMode_ClampToEdge;
    sd.magFilter = WGPUFilterMode_Linear;
    sd.minFilter = WGPUFilterMode_Linear;
    sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    sd.lodMinClamp = 0.0f;
    sd.lodMaxClamp = 32.0f;   // 0 非法（wgpu 零初始化三坑）
    sd.maxAnisotropy = 1;
    impl_->sampler = wgpuDeviceCreateSampler(impl_->device, &sd);

    WGPUShaderSourceWGSL wgsl{};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code.data = kBlitWgsl;
    wgsl.code.length = std::char_traits<char>::length(kBlitWgsl);
    WGPUShaderModuleDescriptor smd{};
    smd.nextInChain = &wgsl.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(impl_->device, &smd);
    if (!module) {
        enterFallbackMode();
        return false;
    }

    // 不混合（blend = nullptr 即 replace 语义：WebGPU 无 Replace 枚举）
    WGPUColorTargetState target{};
    target.format = impl_->surface_format;
    target.blend = nullptr;
    WGPUFragmentState frag{};
    frag.module = module;
    frag.entryPoint = sv("fs_main");
    frag.targetCount = 1;
    frag.targets = &target;

    WGPURenderPipelineDescriptor pd{};
    pd.vertex.module = module;
    pd.vertex.entryPoint = sv("vs_main");
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.fragment = &frag;
    pd.depthStencil = nullptr;
    // multisample 零初始化坑：count 必须 1（0 非法）、mask 全 1（0 非法）
    pd.multisample.count = 1;
    pd.multisample.mask = static_cast<WGPUFlags>(~WGPUFlags(0));
    impl_->pipeline = wgpuDeviceCreateRenderPipeline(impl_->device, &pd);
    wgpuShaderModuleRelease(module);
    if (!impl_->pipeline) {
        enterFallbackMode();
        return false;
    }

    WGPUBufferDescriptor bd{};
    bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    bd.size = 16;
    impl_->uniform_buf = wgpuDeviceCreateBuffer(impl_->device, &bd);
    if (!impl_->uniform_buf || !impl_->sampler) {
        enterFallbackMode();
        return false;
    }

    // 当前图（晚于首帧 setImage 到来时在此上传）
    if (!image_.isNull()) {
        if (!impl_->uploadImage(image_, &impl_->image_texture, &impl_->image_view)) {
            enterFallbackMode();
            return false;
        }
    }
    return true;
#else
    enterFallbackMode();
    return false;
#endif
}

void WgpuImageViewer::renderFrame() {
#ifdef TG_APP_HAS_WGPU
    if (!ensureWgpu() || wgpuFailed_ || !impl_) return;

    // surface 尺寸（retina：物理像素）
    const double dpr = devicePixelRatioF();
    const uint32_t pw = std::max(1u, (uint32_t)std::lround(width() * dpr));
    const uint32_t ph = std::max(1u, (uint32_t)std::lround(height() * dpr));

    WGPUSurfaceConfiguration sc{};
    sc.device = impl_->device;
    sc.format = impl_->surface_format;
    sc.usage = WGPUTextureUsage_RenderAttachment;
    sc.width = pw;
    sc.height = ph;
    sc.alphaMode = WGPUCompositeAlphaMode_Opaque;
    sc.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(impl_->surface, &sc);
#if defined(__APPLE__)
    if (impl_->metal_layer) {
        impl_->metal_layer.contentsScale = dpr;
        impl_->metal_layer.drawableSize = CGSizeMake(pw, ph);
    }
#endif

    WGPUSurfaceTexture st{};
    wgpuSurfaceGetCurrentTexture(impl_->surface, &st);
    if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        if (st.texture) wgpuTextureRelease(st.texture);
        return;
    }
    WGPUTextureView target = wgpuTextureCreateView(st.texture, nullptr);
    if (!target) {
        wgpuTextureRelease(st.texture);
        return;
    }

    // 绑定：无图时用占位文字纹理
    WGPUTextureView view = impl_->image_view;
    if (!view) {
        if (!impl_->placeholder_view) {
            QImage ph(240, 60, QImage::Format_RGBA8888);
            ph.fill(Qt::transparent);
            QPainter p(&ph);
            p.setPen(QColor(120, 120, 120));
            p.drawText(ph.rect(), Qt::AlignCenter, QStringLiteral("No image"));
            p.end();
            if (!impl_->uploadImage(ph, &impl_->placeholder_texture,
                                    &impl_->placeholder_view)) {
                wgpuTextureViewRelease(target);
                wgpuTextureRelease(st.texture);
                return;
            }
        }
        view = impl_->placeholder_view;
    }

    // uniform：(scale.xy, panX, -panY)。panY_ 沿用旧 GL 查看器的 "Y 向上
    // NDC" 约定；本管线的 quad.y=+1 是图像上沿（wgpu Metal 风格），取负换轴。
    // 占位图固定小尺度居中。
    float uni[4];
    if (view == impl_->placeholder_view) {
        uni[0] = 0.4f;
        uni[1] = 0.4f * (60.0f / 240.0f) *
                 (float)height() / std::max(1.0f, (float)width());
        uni[2] = 0.0f;
        uni[3] = 0.0f;
    } else {
        float sx, sy;
        quadScale(sx, sy);
        uni[0] = sx;
        uni[1] = sy;
        uni[2] = panX_;
        uni[3] = -panY_;
    }
    wgpuQueueWriteBuffer(impl_->queue, impl_->uniform_buf, 0, uni, sizeof(uni));

    WGPUBindGroupLayout bgl = wgpuRenderPipelineGetBindGroupLayout(impl_->pipeline, 0);
    WGPUBindGroupEntry entries[3]{};
    entries[0].binding = 0;
    entries[0].buffer = impl_->uniform_buf;
    entries[0].offset = 0;
    entries[0].size = 16;
    entries[1].binding = 1;
    entries[1].sampler = impl_->sampler;
    entries[2].binding = 2;
    entries[2].textureView = view;
    WGPUBindGroupDescriptor bgd{};
    bgd.layout = bgl;
    bgd.entryCount = 3;
    bgd.entries = entries;
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(impl_->device, &bgd);
    wgpuBindGroupLayoutRelease(bgl);
    if (!bg) {
        wgpuTextureViewRelease(target);
        wgpuTextureRelease(st.texture);
        return;
    }

    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(impl_->device, nullptr);
    WGPURenderPassColorAttachment attach{};
    attach.view = target;
    attach.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;  // 零初始化=0 被当成 3D 切片（wgpu 坑）
    attach.loadOp = WGPULoadOp_Clear;
    attach.storeOp = WGPUStoreOp_Store;
    attach.clearValue = {0.08f, 0.08f, 0.08f, 1.0f};  // #141414
    WGPURenderPassDescriptor rp{};
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &attach;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderSetPipeline(pass, impl_->pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(impl_->queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(enc);
    wgpuSurfacePresent(impl_->surface);

    wgpuBindGroupRelease(bg);
    wgpuTextureViewRelease(target);
    wgpuTextureRelease(st.texture);
#endif
}

void WgpuImageViewer::paintEvent(QPaintEvent*) {
#ifdef TG_APP_HAS_WGPU
    if (!wgpuFailed_) {
        renderFrame();
        return;
    }
#endif
    if (fallback_) {
        // QLabel 降级视图自绘；此处无需再画
        return;
    }
    // wgpu 编译未开启（无 TG_APP_HAS_WGPU）时的静态占位
    QPainter p(this);
    p.fillRect(rect(), QColor(20, 20, 20));
    p.setPen(QColor(120, 120, 120));
    p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No image"));
}

void WgpuImageViewer::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (fallback_) {
        fallback_->setGeometry(rect());
        updateFallback();
    }
}

void WgpuImageViewer::clampPan() {
    float sx, sy;
    quadScale(sx, sy);
    if (sx < 1.0f) {
        panX_ = 0.0f;
    } else {
        panX_ = std::clamp(panX_, -(sx - 1.0f), sx - 1.0f);
    }
    if (sy < 1.0f) {
        panY_ = 0.0f;
    } else {
        panY_ = std::clamp(panY_, -(sy - 1.0f), sy - 1.0f);
    }
}

QPointF WgpuImageViewer::screenToImage(const QPointF& screenPos) const {
    if (image_.isNull()) return QPointF(-1, -1);

    const float vw = static_cast<float>(width());
    const float vh = static_cast<float>(height());
    const float iw = static_cast<float>(image_.width());
    const float ih = static_cast<float>(image_.height());

    float sx, sy;
    quadScale(sx, sy);

    // Screen -> NDC（Y 向上，与旧 GL 查看器一致的约定）
    float ndcX = (2.0f * screenPos.x() / vw) - 1.0f;
    float ndcY = 1.0f - (2.0f * screenPos.y() / vh);

    float posNdcX = (ndcX - panX_) / sx;
    float posNdcY = (ndcY - panY_) / (-sy);

    float imgX = (posNdcX + 1.0f) * 0.5f * iw;
    float imgY = (1.0f - posNdcY) * 0.5f * ih;

    return QPointF(imgX, imgY);
}

void WgpuImageViewer::updatePixelInfo(const QPoint& mousePos) {
    if (image_.isNull()) {
        emit pixelInfoChanged(QString());
        return;
    }

    QPointF imgPos = screenToImage(mousePos);
    int px = static_cast<int>(std::round(imgPos.x()));
    int py = static_cast<int>(std::round(imgPos.y()));

    if (px < 0 || px >= image_.width() || py < 0 || py >= image_.height()) {
        emit pixelInfoChanged(QString("x: -, y: - (outside image)"));
        return;
    }

    QColor c = image_.pixelColor(px, py);
    QString text;
    if (image_.format() == QImage::Format_Grayscale8 || image_.format() == QImage::Format_Grayscale16) {
        text = QString("x: %1, y: %2 | Gray: %3").arg(px).arg(py).arg(c.value());
    } else {
        text = QString("x: %1, y: %2 | R: %3 G: %4 B: %5%6")
                   .arg(px).arg(py)
                   .arg(c.red()).arg(c.green()).arg(c.blue())
                   .arg(c.alpha() < 255 ? QString(" A: %1").arg(c.alpha()) : QString());
    }
    emit pixelInfoChanged(text);
}

void WgpuImageViewer::wheelEvent(QWheelEvent* event) {
    if (image_.isNull()) return;

    QPointF mousePos = event->position();
    QPointF imgBefore = screenToImage(mousePos);

    float factor = (event->angleDelta().y() > 0) ? 1.15f : 1.0f / 1.15f;
    zoom_ = std::clamp(zoom_ * factor, MIN_ZOOM, MAX_ZOOM);

    QPointF imgAfter = screenToImage(mousePos);

    float iw = static_cast<float>(image_.width());
    float ih = static_cast<float>(image_.height());
    float sx, sy;
    quadScale(sx, sy);

    float dxNdc = (imgAfter.x() - imgBefore.x()) / iw * 2.0f * sx;
    float dyNdc = (imgAfter.y() - imgBefore.y()) / ih * 2.0f * sy;
    panX_ += dxNdc;
    panY_ -= dyNdc;

    clampPan();
    update();
    updatePixelInfo(mousePos.toPoint());
}

void WgpuImageViewer::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        lastDragPos_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void WgpuImageViewer::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_) {
        QPointF delta = event->pos() - lastDragPos_;
        lastDragPos_ = event->pos();

        float vw = static_cast<float>(width());
        float vh = static_cast<float>(height());
        panX_ += 2.0f * delta.x() / vw;
        panY_ -= 2.0f * delta.y() / vh;
        clampPan();
        update();
    }
    updatePixelInfo(event->pos());
}

void WgpuImageViewer::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = false;
        setCursor(Qt::ArrowCursor);
    }
}

void WgpuImageViewer::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        resetView();
    }
}
