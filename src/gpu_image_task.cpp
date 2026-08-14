#include <task_graph/gpu_image_task.hpp>
#include <task_graph/gpu_kernel_library.hpp>
#include <task_graph/gpu_buffer.hpp>
#include <task_graph/gpu_image_ops.hpp>
#include <task_graph/data_types.hpp>
#include <plugin_api.hpp>

namespace task_graph {

namespace {
const char* const kGpuBoxBlurType          = "gpu_box_blur";
const char* const kGpuGaussianBlurType     = "gpu_gaussian_blur";
const char* const kGpuGrayscaleType        = "gpu_grayscale";
const char* const kGpuBrightnessContrastType = "gpu_brightness_contrast";
const char* const kGpuResizeType           = "gpu_resize";
const char* const kGpuComputeType          = "gpu_compute";
}  // namespace

// ====================== GpuImageTaskBase ======================

std::vector<PortSpec> GpuImageTaskBase::input_specs() const {
    // 双输入算子（input_count==2）额外声明 "in2" 端口
    const GpuImageOp* op = GpuKernelLibrary::instance().find(resolve_op_name());
    if (op && op->input_count >= 2) {
        return { PortSpec{"in", "", true}, PortSpec{"in2", "", true} };
    }
    return { PortSpec{"in", "", true} };
}

std::vector<PortSpec> GpuImageTaskBase::output_specs() const {
    return { make_port<Image>("out") };
}

void GpuImageTaskBase::on_init() {
    auto backend = get_gpu_backend();
    if (!backend || !backend->supports_compute()) {
        TG_LOG_WARN(("gpu task '" + id() + "': backend not compute-capable, kernel precompile skipped").c_str());
        return;
    }

    const GpuImageOp* op = GpuKernelLibrary::instance().find(resolve_op_name());
    if (!op) return;

    const std::string lang = backend->kernel_language();
    const std::string& kernel_source =
        (lang == "glsl" && !op->kernel_source_glsl.empty()) ? op->kernel_source_glsl : op->kernel_source;
    if (kernel_source.empty()) return;

    backend->compile_kernel(op->kernel_name, kernel_source);
}

TaskResult GpuImageTaskBase::run_gpu_op(TaskContext& ctx, const std::string& op_name) {
    auto backend = get_gpu_backend();
    if (!backend || !backend->supports_compute()) {
        TG_LOG_ERROR(("gpu task '" + id() + "': no compute-capable GPU backend (got '" +
                      (backend ? backend->get_backend_name() : std::string("null")) +
                      "'); did the app call InitGpuBackend()?").c_str());
        return TaskResult{.status = TaskStatus::FAILED};
    }

    const GpuImageOp* op = GpuKernelLibrary::instance().find(op_name);
    if (!op) {
        TG_LOG_ERROR(("gpu task '" + id() + "': op '" + op_name + "' not found in kernel library").c_str());
        return TaskResult{.status = TaskStatus::FAILED};
    }

    Image input_img;
    if (auto img_opt = ctx.input<Image>("in")) {
        input_img = std::move(*img_opt);
    } else {
#ifdef TASK_GRAPH_ENABLE_OPENCV
        auto mat_opt = ctx.input<cv::Mat>("in");
        if (!mat_opt) {
            TG_LOG_ERROR(("gpu task '" + id() + "': input 'in' is neither Image nor cv::Mat").c_str());
            return TaskResult{.status = TaskStatus::FAILED};
        }
        input_img = Image::from_mat(*mat_opt);
#else
        TG_LOG_ERROR(("gpu task '" + id() + "': input 'in' is not Image (OpenCV disabled, cv::Mat path unavailable)").c_str());
        return TaskResult{.status = TaskStatus::FAILED};
#endif
    }

    // 第二输入（仅双输入算子）：in2 与 in1 尺寸必须一致
    Image input2_img;
    const bool dual_input = (op->input_count >= 2);
    if (dual_input) {
        if (auto img_opt = ctx.input<Image>("in2")) {
            input2_img = std::move(*img_opt);
        } else {
#ifdef TASK_GRAPH_ENABLE_OPENCV
            auto mat_opt = ctx.input<cv::Mat>("in2");
            if (!mat_opt) {
                TG_LOG_ERROR(("gpu task '" + id() + "': input 'in2' is neither Image nor cv::Mat").c_str());
                return TaskResult{.status = TaskStatus::FAILED};
            }
            input2_img = Image::from_mat(*mat_opt);
#else
            TG_LOG_ERROR(("gpu task '" + id() + "': input 'in2' is not Image (OpenCV disabled, cv::Mat path unavailable)").c_str());
            return TaskResult{.status = TaskStatus::FAILED};
#endif
        }
        if (input2_img.width != input_img.width || input2_img.height != input_img.height) {
            TG_LOG_ERROR(("gpu task '" + id() + "': in2 size " +
                          std::to_string(input2_img.width) + "x" + std::to_string(input2_img.height) +
                          " != in size " + std::to_string(input_img.width) + "x" +
                          std::to_string(input_img.height)).c_str());
            return TaskResult{.status = TaskStatus::FAILED};
        }
    }

    // 当前 kernel 仅支持 UINT8 输入，通道数由算子声明（RGB=3 / RGBA=4）
    auto channel_error = [&](const char* port, int got, int want) {
        TG_LOG_ERROR(("gpu task '" + id() + "': input '" + port + "' channels=" +
                      std::to_string(got) + ", expected " + std::to_string(want) +
                      " (dtype=" + std::to_string(static_cast<int>(input_img.data_type)) + ")").c_str());
    };
    if (input_img.channels != op->input_channels || input_img.data_type != DataType::UINT8) {
        channel_error("in", input_img.channels, op->input_channels);
        return TaskResult{.status = TaskStatus::FAILED};
    }
    if (dual_input && (input2_img.channels != op->input2_channels || input2_img.data_type != DataType::UINT8)) {
        channel_error("in2", input2_img.channels, op->input2_channels);
        return TaskResult{.status = TaskStatus::FAILED};
    }

    // 确保输入在 GPU（链式传递命中时直接复用上游 gpu_buffer）
    if (!ensure_gpu(input_img)) {
        TG_LOG_ERROR(("gpu task '" + id() + "': ensure_gpu failed (upload to GPU failed)").c_str());
        return TaskResult{.status = TaskStatus::FAILED};
    }
    if (dual_input && !ensure_gpu(input2_img)) {
        TG_LOG_ERROR(("gpu task '" + id() + "': ensure_gpu failed for 'in2'").c_str());
        return TaskResult{.status = TaskStatus::FAILED};
    }

    // 按后端语言选择 kernel 源码（Metal 用 MSL，Vulkan 用 GLSL）
    const std::string lang = backend->kernel_language();
    const std::string& kernel_source =
        (lang == "glsl" && !op->kernel_source_glsl.empty()) ? op->kernel_source_glsl : op->kernel_source;
    if (kernel_source.empty()) {
        TG_LOG_ERROR(("gpu task '" + id() + "': op '" + op_name + "' has no kernel source for language '" +
                      lang + "'").c_str());
        return TaskResult{.status = TaskStatus::FAILED};
    }

    // 编译 kernel（backend 内部缓存，首次编译后续复用）
    uintptr_t kernel = backend->compile_kernel(op->kernel_name, kernel_source);
    if (kernel == 0) {
        TG_LOG_ERROR(("gpu task '" + id() + "': compile_kernel failed for '" + op->kernel_name + "'").c_str());
        return TaskResult{.status = TaskStatus::FAILED};
    }

    // 计算输出尺寸
    int out_w = 0, out_h = 0, out_c = 0;
    op->compute_output_size(input_img, ctx.params(), out_w, out_h, out_c);
    if (out_w <= 0 || out_h <= 0 || out_c <= 0) {
        TG_LOG_ERROR(("gpu task '" + id() + "': invalid output size " +
                      std::to_string(out_w) + "x" + std::to_string(out_h) + "x" +
                      std::to_string(out_c)).c_str());
        return TaskResult{.status = TaskStatus::FAILED};
    }

    // 分配输出 buffer
    size_t out_bytes = static_cast<size_t>(out_w) * out_h * out_c;
    uintptr_t dst_handle = backend->allocate_gpu_memory(out_bytes);
    if (dst_handle == 0) {
        TG_LOG_ERROR(("gpu task '" + id() + "': allocate_gpu_memory failed (" +
                      std::to_string(out_bytes) + " bytes)").c_str());
        return TaskResult{.status = TaskStatus::FAILED};
    }

    // 打包 uniform + 计算 dispatch grid
    auto uniform_data = op->pack_uniforms(input_img, ctx.params());
    uint32_t grid_x = 1, grid_y = 1, grid_z = 1;
    op->compute_grid(input_img, ctx.params(), grid_x, grid_y, grid_z);

    // dispatch：单输入 bindings = {in, dst}；双输入 bindings = {in, in2, dst}
    //（uniform 绑定在 bindings 之后：Metal buffer(N) / Vulkan push_constant）
    std::vector<GpuBinding> bindings;
    bindings.reserve(3);
    bindings.push_back({input_img.gpu_handle, 0});
    if (dual_input) {
        bindings.push_back({input2_img.gpu_handle, 0});
    }
    bindings.push_back({dst_handle, 0});

    if (!backend->dispatch(kernel, bindings,
                            uniform_data.data(), uniform_data.size(),
                            grid_x, grid_y, grid_z)) {
        backend->free_gpu_memory(dst_handle);
        TG_LOG_ERROR(("gpu task '" + id() + "': dispatch failed for kernel '" + op->kernel_name + "'").c_str());
        return TaskResult{.status = TaskStatus::FAILED};
    }

    // 构造输出 Image（GPU-resident，location=GPU 支持链式传递）
    Image output;
    output.width = out_w;
    output.height = out_h;
    output.channels = out_c;
    output.pixel_format = (out_c == 1) ? PixelFormat::GRAY : input_img.pixel_format;
    output.data_type = input_img.data_type;
    output.gpu_handle = dst_handle;
    output.gpu_buffer = std::make_shared<GpuBuffer>(dst_handle, out_bytes, backend);
    output.location = MemoryLocation::GPU;

    return TaskResult{.status = TaskStatus::COMPLETED, .value = output};
}

// ====================== GpuBoxBlurTask ======================

const std::string& GpuBoxBlurTask::type() const {
    static const std::string t(kGpuBoxBlurType);
    return t;
}

TaskResult GpuBoxBlurTask::execute(TaskContext& ctx) {
    return run_gpu_op(ctx, kGpuBoxBlurType);
}

std::vector<ParamSpec> GpuBoxBlurTask::param_specs() const {
    const GpuImageOp* op = GpuKernelLibrary::instance().find(kGpuBoxBlurType);
    return op ? op->params : std::vector<ParamSpec>{};
}

// ====================== GpuGaussianBlurTask ======================

const std::string& GpuGaussianBlurTask::type() const {
    static const std::string t(kGpuGaussianBlurType);
    return t;
}

TaskResult GpuGaussianBlurTask::execute(TaskContext& ctx) {
    return run_gpu_op(ctx, kGpuGaussianBlurType);
}

std::vector<ParamSpec> GpuGaussianBlurTask::param_specs() const {
    const GpuImageOp* op = GpuKernelLibrary::instance().find(kGpuGaussianBlurType);
    return op ? op->params : std::vector<ParamSpec>{};
}

// ====================== GpuGrayscaleTask ======================

const std::string& GpuGrayscaleTask::type() const {
    static const std::string t(kGpuGrayscaleType);
    return t;
}

TaskResult GpuGrayscaleTask::execute(TaskContext& ctx) {
    return run_gpu_op(ctx, kGpuGrayscaleType);
}

std::vector<ParamSpec> GpuGrayscaleTask::param_specs() const {
    const GpuImageOp* op = GpuKernelLibrary::instance().find(kGpuGrayscaleType);
    return op ? op->params : std::vector<ParamSpec>{};
}

// ====================== GpuBrightnessContrastTask ======================

const std::string& GpuBrightnessContrastTask::type() const {
    static const std::string t(kGpuBrightnessContrastType);
    return t;
}

TaskResult GpuBrightnessContrastTask::execute(TaskContext& ctx) {
    return run_gpu_op(ctx, kGpuBrightnessContrastType);
}

std::vector<ParamSpec> GpuBrightnessContrastTask::param_specs() const {
    const GpuImageOp* op = GpuKernelLibrary::instance().find(kGpuBrightnessContrastType);
    return op ? op->params : std::vector<ParamSpec>{};
}

// ====================== GpuResizeTask ======================

const std::string& GpuResizeTask::type() const {
    static const std::string t(kGpuResizeType);
    return t;
}

TaskResult GpuResizeTask::execute(TaskContext& ctx) {
    return run_gpu_op(ctx, kGpuResizeType);
}

std::vector<ParamSpec> GpuResizeTask::param_specs() const {
    const GpuImageOp* op = GpuKernelLibrary::instance().find(kGpuResizeType);
    return op ? op->params : std::vector<ParamSpec>{};
}

// ====================== GpuComputeTask ======================

GpuComputeTask::GpuComputeTask(const std::string& id, const std::string& op_name,
                                const TaskConfig& config)
    : GpuImageTaskBase(id, config), op_name_(op_name), type_str_(kGpuComputeType) {}

const std::string& GpuComputeTask::type() const {
    return type_str_;
}

TaskResult GpuComputeTask::execute(TaskContext& ctx) {
    return run_gpu_op(ctx, op_name_);
}

std::vector<ParamSpec> GpuComputeTask::param_specs() const {
    const GpuImageOp* op = GpuKernelLibrary::instance().find(op_name_);
    return op ? op->params : std::vector<ParamSpec>{};
}

std::string GpuComputeTask::resolve_op_name() const {
    return op_name_;
}

}  // namespace task_graph
