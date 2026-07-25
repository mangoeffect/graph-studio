#ifdef TASK_GRAPH_ENABLE_OPENCV

#include <task_graph/data_types.hpp>
#include <opencv2/opencv.hpp>
#include <iostream>

bool test_from_mat() {
    std::cout << "Test: Image::from_mat()... ";

    cv::Mat mat(100, 200, CV_8UC3, cv::Scalar(100, 150, 200));
    
    task_graph::Image img = task_graph::Image::from_mat(mat);
    
    if (!img.valid()) {
        std::cout << "FAILED (image invalid)" << std::endl;
        return false;
    }

    if (img.width != 200 || img.height != 100) {
        std::cout << "FAILED (size mismatch: " << img.width << "x" << img.height << ")" << std::endl;
        return false;
    }

    if (img.channels != 3) {
        std::cout << "FAILED (channels mismatch)" << std::endl;
        return false;
    }

    if (img.pixel_format != task_graph::PixelFormat::BGR) {
        std::cout << "FAILED (pixel format should be BGR)" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_to_mat() {
    std::cout << "Test: Image::to_mat()... ";

    task_graph::Image img(320, 240, 3, task_graph::PixelFormat::BGR);
    
    cv::Mat mat = img.to_mat();
    
    if (mat.empty()) {
        std::cout << "FAILED (mat is empty)" << std::endl;
        return false;
    }

    if (mat.cols != 320 || mat.rows != 240) {
        std::cout << "FAILED (size mismatch)" << std::endl;
        return false;
    }

    if (mat.channels() != 3) {
        std::cout << "FAILED (channels mismatch)" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_roundtrip() {
    std::cout << "Test: cv::Mat <-> Image roundtrip... ";

    cv::Mat original(64, 64, CV_8UC3);
    cv::randu(original, 0, 255);
    
    task_graph::Image img = task_graph::Image::from_mat(original);
    cv::Mat restored = img.to_mat();
    
    if (cv::countNonZero(original != restored) > 0) {
        std::cout << "FAILED (data mismatch after roundtrip)" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_gray_image() {
    std::cout << "Test: Gray image conversion... ";

    cv::Mat mat(100, 100, CV_8UC1, cv::Scalar(128));
    
    task_graph::Image img = task_graph::Image::from_mat(mat);
    
    if (img.channels != 1 || img.pixel_format != task_graph::PixelFormat::GRAY) {
        std::cout << "FAILED (gray image format error)" << std::endl;
        return false;
    }

    cv::Mat restored = img.to_mat();
    if (restored.channels() != 1) {
        std::cout << "FAILED (restored should be single channel)" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

int main() {
    std::cout << "=== OpenCV Conversion Tests ===\n" << std::endl;

    bool all_passed = true;
    all_passed &= test_from_mat();
    all_passed &= test_to_mat();
    all_passed &= test_roundtrip();
    all_passed &= test_gray_image();

    std::cout << "\n=== All tests " << (all_passed ? "PASSED" : "FAILED") << " ===" << std::endl;
    return all_passed ? 0 : 1;
}

#else

#include <iostream>

int main() {
    std::cout << "OpenCV support not enabled. Build with -DTASK_GRAPH_ENABLE_OPENCV=ON to run these tests." << std::endl;
    return 0;
}

#endif
