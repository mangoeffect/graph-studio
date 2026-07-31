#include <task_graph/data_types.hpp>
#include <task_graph/gpu_image_ops.hpp>
#if TASK_GRAPH_ENABLE_METAL
#include <task_graph/gpu_backends/metal_backend.hpp>
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

#if TASK_GRAPH_ENABLE_METAL
    all_passed &= test_metal_backend_basic();
    all_passed &= test_metal_upload_download();
#endif

    std::cout << "\n=== All tests " << (all_passed ? "PASSED" : "FAILED") << " ===" << std::endl;
    return all_passed ? 0 : 1;
}
