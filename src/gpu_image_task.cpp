#include <task_graph/gpu_image_task.hpp>
#include <task_graph/gpu_kernel_library.hpp>
#include <task_graph/gpu_buffer.hpp>
#include <task_graph/gpu_image_ops.hpp>
#include <task_graph/data_types.hpp>

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
    return { make_port<Image>("in") };
}

std::vector<PortSpec> GpuImageTaskBase::output_specs() const {
    return { make_port<Image>("out") };
}

void GpuImageTaskBase::init() {
    auto backend = get_gpu_backend();
    if (!backend || !backend->supports_compute()) return;

    const GpuImageOp* op = GpuKernelLibrary::instance().find(type());
    if (!op) return;

    backend->compile_kernel(op->kernel_name, op->kernel_source);
}

TaskResult GpuImageTaskBase::run_gpu_op(TaskContext& ctx, const std::string& op_name) {
    auto backend = get_gpu_backend();
    if (!backend || !backend->supports_compute()) {
        return TaskResult{.status = TaskStatus::FAILED};
    }

    const GpuImageOp* op = GpuKernelLibrary::instance().find(op_name);
    if (!op) {
        return TaskResult{.status = TaskStatus::FAILED};
    }

    auto input_opt = ctx.input<Image>("in");
    if (!input_opt) {
        return TaskResult{.status = TaskStatus::FAILED};
    }
    Image input_img = std::move(*input_opt);

    // 当前 kernel 仅支持 RGB uint8 输入
    if (input_img.channels != 3 || input_img.data_type != DataType::UINT8) {
        return TaskResult{.status = TaskStatus::FAILED};
    }

    // 确保输入在 GPU（链式传递命中时直接复用上游 gpu_buffer）
    if (!ensure_gpu(input_img)) {
        return TaskResult{.status = TaskStatus::FAILED};
    }

    // 编译 kernel（backend 内部缓存，首次编译后续复用）
    uintptr_t kernel = backend->compile_kernel(op->kernel_name, op->kernel_source);
    if (kernel == 0) {
        return TaskResult{.status = TaskStatus::FAILED};
    }

    // 计算输出尺寸
    int out_w = 0, out_h = 0, out_c = 0;
    op->compute_output_size(input_img, ctx.params(), out_w, out_h, out_c);
    if (out_w <= 0 || out_h <= 0 || out_c <= 0) {
        return TaskResult{.status = TaskStatus::FAILED};
    }

    // 分配输出 buffer
    size_t out_bytes = static_cast<size_t>(out_w) * out_h * out_c;
    uintptr_t dst_handle = backend->allocate_gpu_memory(out_bytes);
    if (dst_handle == 0) {
        return TaskResult{.status = TaskStatus::FAILED};
    }

    // 打包 uniform + 计算 dispatch grid
    auto uniform_data = op->pack_uniforms(input_img, ctx.params());
    uint32_t grid_x = 1, grid_y = 1, grid_z = 1;
    op->compute_grid(input_img, ctx.params(), grid_x, grid_y, grid_z);

    // dispatch
    std::vector<GpuBinding> bindings = {
        {input_img.gpu_handle, 0},
        {dst_handle, 0}
    };

    if (!backend->dispatch(kernel, bindings,
                            uniform_data.data(), uniform_data.size(),
                            grid_x, grid_y, grid_z)) {
        backend->free_gpu_memory(dst_handle);
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

void GpuComputeTask::init() {
    auto backend = get_gpu_backend();
    if (!backend || !backend->supports_compute()) return;

    const GpuImageOp* op = GpuKernelLibrary::instance().find(op_name_);
    if (!op) return;

    backend->compile_kernel(op->kernel_name, op->kernel_source);
}

}  // namespace task_graph
