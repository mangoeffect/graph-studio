#include <task_graph/data_types.hpp>
#include <task_graph/gpu_image_ops.hpp>
#include <plugin_api.hpp>
#if TASK_GRAPH_ENABLE_METAL
#include <task_graph/gpu_backends/metal_backend.hpp>
#include <task_graph/gpu_image_task.hpp>
#include <task_graph/gpu_kernel_library.hpp>
#include <task_graph/dag.hpp>
#include <task_graph/executor.hpp>
#include <task_graph/task.hpp>
#include <cmath>
#include <vector>
#endif
#include <iostream>

bool test_memory_location() {
    std::cout << "Test: Memory location enum... ";

    task_graph::Image img(100, 100, 3);
    
    if (img.location != task_graph::MemoryLocation::CPU) {
        std::cout << "FAILED (default should be CPU)" << std::endl;
        return false;
    }

    if (!img.is_on_cpu()) {
        std::cout << "FAILED (should be on CPU)" << std::endl;
        return false;
    }

    if (img.is_on_gpu()) {
        std::cout << "FAILED (should not be on GPU)" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_gpu_backend_interface() {
    std::cout << "Test: GPU backend interface... ";

    auto backend = task_graph::get_gpu_backend();
    
    if (!backend) {
        std::cout << "FAILED (backend should not be null)" << std::endl;
        return false;
    }

    std::string name = backend->get_backend_name();
    if (name != "null") {
        std::cout << "FAILED (default backend should be 'null')" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_member_to_gpu() {
    std::cout << "Test: img.to_gpu() member function... ";

    task_graph::Image img(100, 100, 3);

    bool result = img.to_gpu();
    if (result) {
        std::cout << "FAILED (to_gpu should return false with null backend)" << std::endl;
        return false;
    }

    if (img.is_on_gpu()) {
        std::cout << "FAILED (image should not be on GPU)" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_member_to_cpu() {
    std::cout << "Test: img.to_cpu() member function... ";

    task_graph::Image img(100, 100, 3);

    bool result = img.to_cpu();
    if (result) {
        std::cout << "FAILED (to_cpu should return false when not on GPU)" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_member_ensure_cpu() {
    std::cout << "Test: img.ensure_cpu() member function... ";

    task_graph::Image img(100, 100, 3);

    bool result = img.ensure_cpu();
    if (!result) {
        std::cout << "FAILED (ensure_cpu should return true for CPU image)" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_member_ensure_gpu() {
    std::cout << "Test: img.ensure_gpu() member function... ";

    task_graph::Image img(100, 100, 3);

    bool result = img.ensure_gpu();
    if (result) {
        std::cout << "FAILED (ensure_gpu should return false with null backend)" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_gpu_handle_field() {
    std::cout << "Test: GPU handle field... ";

    task_graph::Image img(100, 100, 3);
    
    if (img.gpu_handle != 0) {
        std::cout << "FAILED (default gpu_handle should be 0)" << std::endl;
        return false;
    }

    img.gpu_handle = 0x12345678;
    if (img.gpu_handle != 0x12345678) {
        std::cout << "FAILED (gpu_handle should be settable)" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

// ===== init() 去重测试（不依赖 Metal） =====

class InitCounterTask : public task_graph::IPluginTask {
public:
    using INode::INode;
    int init_count = 0;
    const std::string& type() const override {
        static const std::string t("init_counter");
        return t;
    }
    task_graph::TaskResult execute(task_graph::TaskContext&) override {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED};
    }
protected:
    void on_init() override { ++init_count; }
};

bool test_init_dedup() {
    std::cout << "Test: init() dedup... ";

    InitCounterTask task("test");
    task.init();
    task.init();
    task.init();

    if (task.init_count != 1) {
        std::cout << "FAILED (on_init called " << task.init_count << " times, expected 1)" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

#if TASK_GRAPH_ENABLE_METAL
bool test_metal_backend_basic() {
    std::cout << "Test: Metal backend basic init... ";

    auto backend = std::make_shared<task_graph::MetalGpuBackend>();
    bool result = backend->init();

    if (!result) {
        std::cout << "FAILED (init failed, Metal not available)" << std::endl;
        return false;
    }

    if (backend->get_backend_name() != "metal") {
        std::cout << "FAILED (wrong backend name)" << std::endl;
        backend->shutdown();
        return false;
    }

    if (!backend->is_available()) {
        std::cout << "FAILED (backend reports not available)" << std::endl;
        backend->shutdown();
        return false;
    }

    // Test allocate
    uintptr_t handle = backend->allocate_gpu_memory(1024);
    if (handle == 0) {
        std::cout << "FAILED (allocation failed)" << std::endl;
        backend->shutdown();
        return false;
    }

    // Free
    backend->free_gpu_memory(handle);
    backend->shutdown();

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_metal_upload_download() {
    std::cout << "Test: Metal upload/download... ";

    auto backend = std::make_shared<task_graph::MetalGpuBackend>();
    if (!backend->init()) {
        std::cout << "SKIPPED (Metal not available)" << std::endl;
        backend->shutdown();
        return true;
    }

    task_graph::Image img(32, 32, 3);
    // Fill test pattern
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            for (int c = 0; c < 3; c++) {
                img.ptr()[y * 32 * 3 + x * 3 + c] = (uint8_t)(x + y + c);
            }
        }
    }

    task_graph::set_gpu_backend(backend);
    bool uploaded = task_graph::to_gpu(img);
    if (!uploaded) {
        std::cout << "FAILED (upload failed)" << std::endl;
        backend->shutdown();
        return false;
    }

    if (!img.is_on_gpu()) {
        std::cout << "FAILED (image not marked as on GPU)" << std::endl;
        backend->shutdown();
        return false;
    }

    // Clear CPU data and download back
    img.data->clear();
    bool downloaded = task_graph::to_cpu(img);
    if (!downloaded) {
        std::cout << "FAILED (download failed)" << std::endl;
        backend->shutdown();
        return false;
    }

    // Verify data integrity
    bool ok = true;
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            for (int c = 0; c < 3; c++) {
                uint8_t expected = (uint8_t)(x + y + c);
                if (img.ptr()[y * 32 * 3 + x * 3 + c] != expected) {
                    ok = false;
                    break;
                }
            }
        }
    }

    if (!ok) {
        std::cout << "FAILED (data mismatch after round trip)" << std::endl;
        backend->shutdown();
        return false;
    }

    backend->shutdown();
    std::cout << "PASSED" << std::endl;
    return true;
}

// ===== Metal Compute 端到端测试 =====

static std::vector<uint8_t> cpu_box_blur(const uint8_t* src, int W, int H,
                                          int ch, int radius) {
    std::vector<uint8_t> dst(W * H * ch);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            for (int c = 0; c < ch; c++) {
                uint32_t sum = 0, count = 0;
                for (int dy = -radius; dy <= radius; dy++)
                    for (int dx = -radius; dx <= radius; dx++) {
                        int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && nx < W && ny >= 0 && ny < H) {
                            sum += src[(ny * W + nx) * ch + c];
                            count++;
                        }
                    }
                dst[(y * W + x) * ch + c] = static_cast<uint8_t>(sum / count);
            }
    return dst;
}

bool test_metal_compute_box_blur() {
    std::cout << "Test: Metal compute box_blur (vs CPU reference)... ";

    auto backend = std::make_shared<task_graph::MetalGpuBackend>();
    if (!backend->init()) {
        std::cout << "SKIPPED (Metal not available)" << std::endl;
        backend->shutdown();
        return true;
    }
    task_graph::set_gpu_backend(backend);

    const int W = 32, H = 32;
    task_graph::Image img(W, H, 3);
    for (int i = 0; i < W * H * 3; i++)
        img.ptr()[i] = static_cast<uint8_t>((i * 7 + 13) % 256);

    auto ref = cpu_box_blur(img.ptr(), W, H, 3, 1);

    task_graph::GpuBoxBlurTask task("blur");
    task_graph::TaskParams params;
    params.set_int("kernel_size", 3);
    std::unordered_map<std::string, std::any> inputs{{"in", img}};
    task_graph::TaskContext ctx(params, {}, {}, std::move(inputs));

    auto result = task.execute(ctx);
    if (!result.is_success()) {
        std::cout << "FAILED (execute failed)" << std::endl;
        backend->shutdown();
        return false;
    }

    task_graph::Image out = std::any_cast<task_graph::Image>(result.value);
    if (!out.is_on_gpu()) {
        std::cout << "FAILED (output not on GPU)" << std::endl;
        backend->shutdown();
        return false;
    }

    task_graph::ensure_cpu(out);

    int mismatches = 0;
    for (int i = 0; i < W * H * 3; i++) {
        if (std::abs((int)out.ptr()[i] - (int)ref[i]) > 1) mismatches++;
    }

    if (mismatches > 0) {
        std::cout << "FAILED (" << mismatches << " pixel mismatches)" << std::endl;
        backend->shutdown();
        return false;
    }

    backend->shutdown();
    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_metal_chain_gpu_pass() {
    std::cout << "Test: Metal chain GPU pass (blur -> grayscale)... ";

    auto backend = std::make_shared<task_graph::MetalGpuBackend>();
    if (!backend->init()) {
        std::cout << "SKIPPED (Metal not available)" << std::endl;
        backend->shutdown();
        return true;
    }
    task_graph::set_gpu_backend(backend);

    const int W = 32, H = 32;
    task_graph::Image img(W, H, 3);
    for (int i = 0; i < W * H * 3; i++)
        img.ptr()[i] = static_cast<uint8_t>((i * 7 + 13) % 256);

    task_graph::GpuBoxBlurTask blur_task("blur");
    std::unordered_map<std::string, std::any> in1{{"in", img}};
    task_graph::TaskContext ctx1({}, {}, {}, std::move(in1));
    auto r1 = blur_task.execute(ctx1);
    if (!r1.is_success()) {
        std::cout << "FAILED (blur step failed)" << std::endl;
        backend->shutdown();
        return false;
    }
    task_graph::Image blurred = std::any_cast<task_graph::Image>(r1.value);

    if (blurred.location != task_graph::MemoryLocation::GPU) {
        std::cout << "FAILED (blur output not GPU-resident)" << std::endl;
        backend->shutdown();
        return false;
    }

    task_graph::GpuGrayscaleTask gray_task("gray");
    std::unordered_map<std::string, std::any> in2{{"in", blurred}};
    task_graph::TaskContext ctx2({}, {}, {}, std::move(in2));
    auto r2 = gray_task.execute(ctx2);
    if (!r2.is_success()) {
        std::cout << "FAILED (grayscale step failed)" << std::endl;
        backend->shutdown();
        return false;
    }
    task_graph::Image gray = std::any_cast<task_graph::Image>(r2.value);

    if (gray.channels != 1) {
        std::cout << "FAILED (grayscale output channels=" << gray.channels << ")" << std::endl;
        backend->shutdown();
        return false;
    }

    task_graph::ensure_cpu(gray);

    bool ok = true;
    for (int i = 0; i < W * H; i++) {
        if (gray.ptr()[i] > 255) { ok = false; break; }
    }

    if (!ok) {
        std::cout << "FAILED (grayscale data invalid)" << std::endl;
        backend->shutdown();
        return false;
    }

    backend->shutdown();
    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_metal_dag_integration() {
    std::cout << "Test: Metal DAG integration (src -> blur -> gray)... ";

    auto backend = std::make_shared<task_graph::MetalGpuBackend>();
    if (!backend->init()) {
        std::cout << "SKIPPED (Metal not available)" << std::endl;
        backend->shutdown();
        return true;
    }
    task_graph::set_gpu_backend(backend);

    task_graph::DAG dag;

    auto src = std::make_shared<task_graph::Task>("src",
        [](task_graph::TaskContext&) {
            task_graph::Image img(32, 32, 3);
            for (int i = 0; i < 32 * 32 * 3; i++)
                img.ptr()[i] = static_cast<uint8_t>((i * 7 + 13) % 256);
            return task_graph::TaskResult{
                .status = task_graph::TaskStatus::COMPLETED, .value = img};
        });
    dag.add_task(src);

    dag.add_plugin_task("blur", "gpu_box_blur");
    dag.add_plugin_task("gray", "gpu_grayscale");
    dag.connect("src", "blur");
    dag.connect("blur", "gray");

    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    if (!results["gray"].is_success()) {
        std::cout << "FAILED (gray task failed)" << std::endl;
        backend->shutdown();
        return false;
    }

    task_graph::Image out = std::any_cast<task_graph::Image>(results["gray"].value);
    task_graph::ensure_cpu(out);

    if (out.channels != 1 || out.width != 32 || out.height != 32) {
        std::cout << "FAILED (output dims/channels wrong)" << std::endl;
        backend->shutdown();
        return false;
    }

    backend->shutdown();
    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_metal_brightness_contrast() {
    std::cout << "Test: Metal brightness_contrast (float params)... ";

    auto backend = std::make_shared<task_graph::MetalGpuBackend>();
    if (!backend->init()) {
        std::cout << "SKIPPED (Metal not available)" << std::endl;
        backend->shutdown();
        return true;
    }
    task_graph::set_gpu_backend(backend);

    const int W = 16, H = 16;
    task_graph::Image img(W, H, 3);
    for (int i = 0; i < W * H * 3; i++)
        img.ptr()[i] = 128;

    task_graph::GpuBrightnessContrastTask task("bc");
    task_graph::TaskParams params;
    params.set_float("brightness", 0.2f);
    params.set_float("contrast", 0.0f);
    std::unordered_map<std::string, std::any> inputs{{"in", img}};
    task_graph::TaskContext ctx(params, {}, {}, std::move(inputs));

    auto result = task.execute(ctx);
    if (!result.is_success()) {
        std::cout << "FAILED (execute failed)" << std::endl;
        backend->shutdown();
        return false;
    }

    task_graph::Image out = std::any_cast<task_graph::Image>(result.value);
    task_graph::ensure_cpu(out);

    // brightness=0.2, contrast=0: val = (128-128)*1 + 128 + 0.2*255 = 179
    bool ok = true;
    for (int i = 0; i < W * H * 3; i++) {
        if (std::abs((int)out.ptr()[i] - 179) > 1) { ok = false; break; }
    }

    if (!ok) {
        std::cout << "FAILED (brightness result mismatch)" << std::endl;
        backend->shutdown();
        return false;
    }

    backend->shutdown();
    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_metal_init_precompile() {
    std::cout << "Test: Metal init() precompile... ";

    auto backend = std::make_shared<task_graph::MetalGpuBackend>();
    if (!backend->init()) {
        std::cout << "SKIPPED (Metal not available)" << std::endl;
        backend->shutdown();
        return true;
    }
    task_graph::set_gpu_backend(backend);

    task_graph::GpuBoxBlurTask task("blur");
    task.init();

    const task_graph::GpuImageOp* op =
        task_graph::GpuKernelLibrary::instance().find("gpu_box_blur");
    uintptr_t kernel = backend->compile_kernel(op->kernel_name, op->kernel_source);
    if (kernel == 0) {
        std::cout << "FAILED (kernel not compiled after init)" << std::endl;
        backend->shutdown();
        return false;
    }

    const int W = 32, H = 32;
    task_graph::Image img(W, H, 3);
    for (int i = 0; i < W * H * 3; i++)
        img.ptr()[i] = static_cast<uint8_t>((i * 7 + 13) % 256);

    auto ref = cpu_box_blur(img.ptr(), W, H, 3, 1);

    task_graph::TaskParams params;
    params.set_int("kernel_size", 3);
    std::unordered_map<std::string, std::any> inputs{{"in", img}};
    task_graph::TaskContext ctx(params, {}, {}, std::move(inputs));

    auto result = task.execute(ctx);
    if (!result.is_success()) {
        std::cout << "FAILED (execute failed)" << std::endl;
        backend->shutdown();
        return false;
    }

    task_graph::Image out = std::any_cast<task_graph::Image>(result.value);
    task_graph::ensure_cpu(out);

    int mismatches = 0;
    for (int i = 0; i < W * H * 3; i++) {
        if (std::abs((int)out.ptr()[i] - (int)ref[i]) > 1) mismatches++;
    }

    if (mismatches > 0) {
        std::cout << "FAILED (" << mismatches << " mismatches)" << std::endl;
        backend->shutdown();
        return false;
    }

    backend->shutdown();
    std::cout << "PASSED" << std::endl;
    return true;
}
#endif

int main() {
    std::cout << "=== GPU Image Operations Tests ===\n" << std::endl;

    bool all_passed = true;
    all_passed &= test_memory_location();
    all_passed &= test_gpu_backend_interface();
    all_passed &= test_member_to_gpu();
    all_passed &= test_member_to_cpu();
    all_passed &= test_member_ensure_cpu();
    all_passed &= test_member_ensure_gpu();
    all_passed &= test_gpu_handle_field();
    all_passed &= test_init_dedup();

#if TASK_GRAPH_ENABLE_METAL
    all_passed &= test_metal_backend_basic();
    all_passed &= test_metal_upload_download();
    all_passed &= test_metal_compute_box_blur();
    all_passed &= test_metal_chain_gpu_pass();
    all_passed &= test_metal_dag_integration();
    all_passed &= test_metal_brightness_contrast();
    all_passed &= test_metal_init_precompile();
#endif

    std::cout << "\n=== All tests " << (all_passed ? "PASSED" : "FAILED") << " ===" << std::endl;
    return all_passed ? 0 : 1;
}
