#include <task_graph/mnn/mnn_engine.hpp>

#include <algorithm>

#include <task_graph/data_types.hpp>

#ifdef TASK_GRAPH_MNN_AVAILABLE

#include <MNN/ErrorCode.hpp>
#include <MNN/ImageProcess.hpp>
#include <MNN/Interpreter.hpp>
#include <MNN/MNNDefine.h>
#include <MNN/Tensor.hpp>

#ifdef __APPLE__
#include <objc/runtime.h>

// MNN Metal 后端的调用官方要求包在 autoreleasepool 里（iOS FAQ）。.cpp 不是
// ObjC++，@autoreleasepool 语法不可用；这两个是 libobjc 的稳定 ABI（ARC 底层
// 实现），SDK 未公开声明，这里手工声明。
extern "C" {
void* objc_autoreleasePoolPush(void);
void objc_autoreleasePoolPop(void* pool);
}

namespace task_graph {
namespace {
struct ScopedAutoreleasePool {
    ScopedAutoreleasePool() : pool_(objc_autoreleasePoolPush()) {}
    ~ScopedAutoreleasePool() { objc_autoreleasePoolPop(pool_); }
    ScopedAutoreleasePool(const ScopedAutoreleasePool&) = delete;
    ScopedAutoreleasePool& operator=(const ScopedAutoreleasePool&) = delete;
    void* pool_;
};
}  // namespace
}  // namespace task_graph
#endif

namespace task_graph {

namespace {

MNN::CV::ImageFormat to_cv_format(MnnSourceFormat format) {
    switch (format) {
        case MnnSourceFormat::GRAY: return MNN::CV::GRAY;
        case MnnSourceFormat::RGB:  return MNN::CV::RGB;
        case MnnSourceFormat::RGBA: return MNN::CV::RGBA;
        case MnnSourceFormat::BGR:
        default:                    return MNN::CV::BGR;
    }
}

MNNForwardType resolve_forward_type(const std::string& device, MNNForwardType& backup) {
    backup = MNN_FORWARD_CPU;
    if (device == "metal") return MNN_FORWARD_METAL;
    if (device == "auto") {
        // auto：Apple 平台优先 Metal，CPU 兜底；其它平台 CPU。
#ifdef __APPLE__
        return MNN_FORWARD_METAL;
#else
        return MNN_FORWARD_CPU;
#endif
    }
    return MNN_FORWARD_CPU;
}

MNN::BackendConfig::PrecisionMode to_precision(const std::string& p) {
    if (p == "high") return MNN::BackendConfig::Precision_High;
    if (p == "low")  return MNN::BackendConfig::Precision_Low;
    return MNN::BackendConfig::Precision_Normal;
}

}  // namespace

struct MnnEngine::Impl {
    MnnEngineOptions opts;
    MNN::Interpreter* net{nullptr};
    MNN::Session* session{nullptr};
    MNN::Tensor* input_tensor{nullptr};
    std::vector<int> input_shape;  // 逻辑 N,C,H,W
};

MnnEngine::MnnEngine() : impl_(std::make_unique<Impl>()) {}
MnnEngine::~MnnEngine() {
    if (impl_->net) {
        MNN::Interpreter::destroy(impl_->net);  // session 由 net 持有
    }
}

const std::vector<int>& MnnEngine::input_shape() const { return impl_->input_shape; }

std::shared_ptr<MnnEngine> MnnEngine::create(const MnnEngineOptions& opts,
                                             std::string& err) {
    auto engine = std::shared_ptr<MnnEngine>(new MnnEngine());
    Impl& impl = *engine->impl_;
    impl.opts = opts;

    impl.net = MNN::Interpreter::createFromFile(opts.model_path.c_str());
    if (!impl.net) {
        err = "MNN: failed to load model '" + opts.model_path + "'";
        return nullptr;
    }

    MNN::ScheduleConfig schedule;
    schedule.type = resolve_forward_type(opts.device, schedule.backupType);
    schedule.numThread = std::max(1, opts.threads);
    MNN::BackendConfig backend;
    backend.precision = to_precision(opts.precision);
    schedule.backendConfig = &backend;

    impl.session = impl.net->createSession(schedule);
    if (!impl.session) {
        err = "MNN: createSession failed (device='" + opts.device +
              "'；后端可能未编入 libMNN，可用 device=cpu 重试)";
        return nullptr;
    }

    impl.input_tensor = impl.net->getSessionInput(impl.session, nullptr);
    if (!impl.input_tensor) {
        err = "MNN: model has no input tensor";
        return nullptr;
    }

    const bool need_resize = opts.input_width > 0 && opts.input_height > 0;
    if (need_resize) {
        impl.net->resizeTensor(impl.input_tensor, 1, impl.input_tensor->channel(),
                               opts.input_height, opts.input_width);
        impl.net->resizeSession(impl.session);
    }

    // 逻辑 NCHW：CAFFE/CAFFE_C4 本身即 NCHW 序；TENSORFLOW(NHWC) 换位。
    std::vector<int> shape = impl.input_tensor->shape();
    if (impl.input_tensor->getDimensionType() == MNN::Tensor::TENSORFLOW &&
        shape.size() == 4) {
        impl.input_shape = {shape[0], shape[3], shape[1], shape[2]};
    } else {
        impl.input_shape = shape;
    }
    return engine;
}

bool MnnEngine::run(const uint8_t* data, int width, int height,
                    MnnSourceFormat format, std::vector<MnnTensorData>& outputs,
                    std::string& err) {
    Impl& impl = *impl_;
    if (!impl.net || !impl.session || !impl.input_tensor) {
        err = "MNN: engine not initialized";
        return false;
    }

    // @autoreleasepool 语义见文件头 ScopedAutoreleasePool 注释
#ifdef __APPLE__
    ScopedAutoreleasePool autorelease_pool;
#endif

    MNN::CV::ImageProcess::Config proc_cfg;
    proc_cfg.filterType = MNN::CV::BILINEAR;
    proc_cfg.sourceFormat = to_cv_format(format);
    proc_cfg.destFormat = MNN::CV::RGB;
    for (int i = 0; i < 3; ++i) {
        proc_cfg.mean[i] = impl.opts.mean[i];
        proc_cfg.normal[i] = impl.opts.scale[i] != 0.f ? 1.f / impl.opts.scale[i] : 1.f;
    }

    std::unique_ptr<MNN::CV::ImageProcess> process(
        MNN::CV::ImageProcess::create(proc_cfg));
    if (!process) {
        err = "MNN: ImageProcess::create failed";
        return false;
    }

    const int input_h = impl.input_tensor->height();
    const int input_w = impl.input_tensor->width();
    MNN::CV::Matrix matrix;
    // 方向：源坐标 = 矩阵 × 目标坐标（MNN 内部对矩阵求逆后做 dest→src 采样），
    // 缩放比必须取 src/dst = width/input_w。此前误用 input_w/width（dst/src），
    // 非等比缩放下采样错位（UltraFace 人脸检测上表现为置信度塌缩、框贴边；
    // 2026-09 face 子模块接入时实测定位：identity 矩阵 + 预缩放输入正常，
    // 倒置 scale 后与 identity 结果一致）。
    matrix.setScale(static_cast<float>(width) / static_cast<float>(input_w),
                    static_cast<float>(height) / static_cast<float>(input_h));
    process->setMatrix(matrix);

    // host 输入张量与 session 输入同 shape/同 layout（CAFFE_C4 等），
    // ImageProcess 填充后 copyFromHostTensor 送入（CPU 后端为同布局拷贝，
    // GPU 后端为上传）。
    std::shared_ptr<MNN::Tensor> input_host(MNN::Tensor::create<float>(
        impl.input_tensor->shape(), nullptr, impl.input_tensor->getDimensionType()));
    if (!input_host) {
        err = "MNN: failed to allocate input tensor";
        return false;
    }

    auto ec = process->convert(data, width, height, 0, input_host.get());
    if (ec != MNN::NO_ERROR) {
        err = "MNN: preprocess convert failed (code " + std::to_string(static_cast<int>(ec)) + ")";
        return false;
    }
    if (!impl.input_tensor->copyFromHostTensor(input_host.get())) {
        err = "MNN: copyFromHostTensor failed";
        return false;
    }

    ec = impl.net->runSession(impl.session);
    if (ec != MNN::NO_ERROR) {
        err = "MNN: runSession failed (code " + std::to_string(static_cast<int>(ec)) + ")";
        return false;
    }

    // GPU 后端 runSession 是异步提交，拷回 host 张量时才真正同步。
    outputs.clear();
    const auto& all_outputs = impl.net->getSessionOutputAll(impl.session);
    for (const auto& [name, tensor] : all_outputs) {
        if (!tensor) continue;
        std::shared_ptr<MNN::Tensor> host(
            new MNN::Tensor(tensor, tensor->getDimensionType(), true));
        if (!tensor->copyToHostTensor(host.get())) {
            err = "MNN: copyToHostTensor failed for output '" + name + "'";
            return false;
        }
        if (host->getType() != halide_type_of<float>()) {
            err = "MNN: output '" + name + "' is not float32（当前仅支持 float 输出模型）";
            return false;
        }
        MnnTensorData td;
        td.name = name;
        td.shape = host->shape();
        const int elems = host->elementSize();
        td.data.assign(host->host<float>(), host->host<float>() + elems);
        outputs.push_back(std::move(td));
    }
    return true;
}

}  // namespace task_graph

#else  // !TASK_GRAPH_MNN_AVAILABLE —— stub：任务保持注册、execute 报错

namespace task_graph {

struct MnnEngine::Impl {};

MnnEngine::MnnEngine() = default;
MnnEngine::~MnnEngine() = default;

const std::vector<int>& MnnEngine::input_shape() const {
    static const std::vector<int> empty;
    return empty;
}

std::shared_ptr<MnnEngine> MnnEngine::create(const MnnEngineOptions&, std::string& err) {
    err = "MNN engine not built — run scripts/build_mnn.py then reconfigure "
          "with -DTASK_GRAPH_ENABLE_MNN=ON";
    return nullptr;
}

bool MnnEngine::run(const uint8_t*, int, int, MnnSourceFormat,
                    std::vector<MnnTensorData>&, std::string& err) {
    err = "MNN engine not built — run scripts/build_mnn.py then reconfigure "
          "with -DTASK_GRAPH_ENABLE_MNN=ON";
    return false;
}

}  // namespace task_graph

#endif  // TASK_GRAPH_MNN_AVAILABLE

// Image 便捷重载：两态共用（只转发的公共 API），pixel_format → source 通道序。
namespace task_graph {

namespace {
MnnSourceFormat image_source_format(const Image& img) {
    switch (img.channels) {
        case 1: return MnnSourceFormat::GRAY;
        case 4: return MnnSourceFormat::RGBA;
        case 3:
        default:
            return img.pixel_format == PixelFormat::RGB ? MnnSourceFormat::RGB
                                                        : MnnSourceFormat::BGR;
    }
}
}  // namespace

bool MnnEngine::run(const Image& image, std::vector<MnnTensorData>& outputs,
                    std::string& err) {
    if (!image.data) {
        err = "MNN: empty image";
        return false;
    }
    Image copy = image;  // ensure_cpu 需要 non-const
    if (!copy.ensure_cpu() || !copy.valid()) {
        err = "MNN: image not available on CPU";
        return false;
    }
    return run(copy.data->data(), copy.width, copy.height,
               image_source_format(copy), outputs, err);
}

// 端口数据类型跨 SO 稳定名注册（任务层移除后随引擎保留——face/matting
// 的端口/后端结果仍在使用这些类型，stub 构建同样注册）
TG_REGISTER_TYPE(MnnTensorData, "task_graph::MnnTensorData");
TG_REGISTER_TYPE(MnnInferenceResult, "task_graph::MnnInferenceResult");
TG_REGISTER_TYPE(MnnClassificationResult, "task_graph::MnnClassificationResult");

}  // namespace task_graph
