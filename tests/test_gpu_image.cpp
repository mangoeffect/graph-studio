#include <task_graph/data_types.hpp>
#include <task_graph/gpu_image_ops.hpp>
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

    std::cout << "\n=== All tests " << (all_passed ? "PASSED" : "FAILED") << " ===" << std::endl;
    return all_passed ? 0 : 1;
}
