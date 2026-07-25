#include <task_graph/task_graph.hpp>
#include <task_graph/dag_serializer.hpp>
#include <task_graph/data_types.hpp>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

bool test_single_blur_filter() {
    std::cout << "Test: Single blur filter execution... ";
    
    task_graph::DAG dag;
    
    auto input_task = std::make_shared<task_graph::Task>("input_image", [](task_graph::IExecutionContext& ctx) {
        cv::Mat mat = cv::Mat::ones(100, 100, CV_8UC3) * 200;
        task_graph::Image img = task_graph::Image::from_mat(mat);
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = img};
    });
    
    dag.add_task(input_task);
    dag.add_plugin_task("my_blur", "blur_filter");
    
    dag.add_dependency("input_image", "my_blur");
    
    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();
    
    auto results = executor.get_results();
    
    bool success = results["input_image"].is_success() && 
                   results["my_blur"].is_success();
    
    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

bool test_multiple_filter_cascade() {
    std::cout << "Test: Multiple filter cascade (GaussianBlur -> Sobel)... ";
    
    task_graph::DAG dag;
    
    auto input_task = std::make_shared<task_graph::Task>("input_image", [](task_graph::IExecutionContext& ctx) {
        cv::Mat mat = cv::Mat::ones(100, 100, CV_8UC3) * 200;
        task_graph::Image img = task_graph::Image::from_mat(mat);
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = img};
    });
    
    dag.add_task(input_task);
    dag.add_plugin_task("gaussian_blur", "gaussian_blur_filter");
    dag.add_plugin_task("sobel_edge", "sobel_filter");
    
    dag.add_dependency("input_image", "gaussian_blur");
    dag.add_dependency("gaussian_blur", "sobel_edge");
    
    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();
    
    auto results = executor.get_results();
    
    bool success = results["input_image"].is_success() && 
                   results["gaussian_blur"].is_success() &&
                   results["sobel_edge"].is_success();
    
    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

bool test_parallel_filters() {
    std::cout << "Test: Parallel filters (same input, different filters)... ";
    
    task_graph::DAG dag;
    
    auto input_task = std::make_shared<task_graph::Task>("input_image", [](task_graph::IExecutionContext& ctx) {
        cv::Mat mat = cv::Mat::ones(100, 100, CV_8UC3) * 200;
        task_graph::Image img = task_graph::Image::from_mat(mat);
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = img};
    });
    
    dag.add_task(input_task);
    dag.add_plugin_task("box_blur", "blur_filter");
    dag.add_plugin_task("median_blur", "median_blur_filter");
    dag.add_plugin_task("bilateral", "bilateral_filter");
    
    dag.add_dependency("input_image", "box_blur");
    dag.add_dependency("input_image", "median_blur");
    dag.add_dependency("input_image", "bilateral");
    
    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();
    
    auto results = executor.get_results();
    
    bool success = results["input_image"].is_success() && 
                   results["box_blur"].is_success() &&
                   results["median_blur"].is_success() &&
                   results["bilateral"].is_success();
    
    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

bool test_long_filter_pipeline() {
    std::cout << "Test: Long filter pipeline (5 stages)... ";
    
    task_graph::DAG dag;
    
    auto input_task = std::make_shared<task_graph::Task>("input_image", [](task_graph::IExecutionContext& ctx) {
        cv::Mat mat = cv::Mat::ones(200, 200, CV_8UC3) * 150;
        task_graph::Image img = task_graph::Image::from_mat(mat);
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = img};
    });
    
    dag.add_task(input_task);
    dag.add_plugin_task("stage1_box", "box_filter");
    dag.add_plugin_task("stage2_gaussian", "gaussian_blur_filter");
    dag.add_plugin_task("stage3_median", "median_blur_filter");
    dag.add_plugin_task("stage4_sobel", "sobel_filter");
    dag.add_plugin_task("stage5_laplacian", "laplacian_filter");
    
    dag.add_dependency("input_image", "stage1_box");
    dag.add_dependency("stage1_box", "stage2_gaussian");
    dag.add_dependency("stage2_gaussian", "stage3_median");
    dag.add_dependency("stage3_median", "stage4_sobel");
    dag.add_dependency("stage4_sobel", "stage5_laplacian");
    
    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();
    
    auto results = executor.get_results();
    
    bool success = results["input_image"].is_success() && 
                   results["stage1_box"].is_success() &&
                   results["stage2_gaussian"].is_success() &&
                   results["stage3_median"].is_success() &&
                   results["stage4_sobel"].is_success() &&
                   results["stage5_laplacian"].is_success();
    
    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

bool test_same_type_multiple_instances() {
    std::cout << "Test: Same type multiple instances... ";
    
    task_graph::DAG dag;
    
    auto input_task = std::make_shared<task_graph::Task>("input_image", [](task_graph::IExecutionContext& ctx) {
        cv::Mat mat = cv::Mat::ones(100, 100, CV_8UC3) * 200;
        task_graph::Image img = task_graph::Image::from_mat(mat);
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = img};
    });
    
    dag.add_task(input_task);
    dag.add_plugin_task("blur_instance_1", "blur_filter");
    dag.add_plugin_task("blur_instance_2", "blur_filter");
    dag.add_plugin_task("blur_instance_3", "blur_filter");
    
    dag.add_dependency("input_image", "blur_instance_1");
    dag.add_dependency("input_image", "blur_instance_2");
    dag.add_dependency("input_image", "blur_instance_3");
    
    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();
    
    auto results = executor.get_results();
    
    bool success = results["input_image"].is_success() && 
                   results["blur_instance_1"].is_success() &&
                   results["blur_instance_2"].is_success() &&
                   results["blur_instance_3"].is_success();
    
    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

bool test_filter_with_custom_ids() {
    std::cout << "Test: Filter with custom IDs via add_plugin_task... ";
    
    task_graph::DAG dag;
    
    auto input_task = std::make_shared<task_graph::Task>("input_image", [](task_graph::IExecutionContext& ctx) {
        cv::Mat mat = cv::Mat::ones(100, 100, CV_8UC3) * 150;
        task_graph::Image img = task_graph::Image::from_mat(mat);
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = img};
    });
    dag.add_task(input_task);
    
    dag.add_plugin_task("preprocessing_blur", "gaussian_blur_filter");
    dag.add_plugin_task("edge_detection", "sobel_filter");
    dag.add_plugin_task("enhancement", "laplacian_filter");
    
    dag.add_dependency("input_image", "preprocessing_blur");
    dag.add_dependency("preprocessing_blur", "edge_detection");
    dag.add_dependency("edge_detection", "enhancement");
    
    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();
    
    auto results = executor.get_results();
    
    bool success = results["input_image"].is_success() &&
                   results["preprocessing_blur"].is_success() &&
                   results["edge_detection"].is_success() &&
                   results["enhancement"].is_success();
    
    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

bool test_image_data_passing() {
    std::cout << "Test: Image data passing between filters... ";
    
    task_graph::DAG dag;
    
    auto input_task = std::make_shared<task_graph::Task>("input_image", [](task_graph::IExecutionContext& ctx) {
        cv::Mat mat = cv::Mat::zeros(100, 100, CV_8UC3);
        cv::rectangle(mat, cv::Rect(20, 20, 60, 60), cv::Scalar(255, 128, 64), -1);
        task_graph::Image img = task_graph::Image::from_mat(mat);
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = img};
    });
    
    dag.add_task(input_task);
    dag.add_plugin_task("blur_stage", "blur_filter");
    dag.add_plugin_task("edge_stage", "sobel_filter");
    
    dag.add_dependency("input_image", "blur_stage");
    dag.add_dependency("blur_stage", "edge_stage");
    
    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();
    
    auto results = executor.get_results();
    
    bool success = results["input_image"].is_success() && 
                   results["blur_stage"].is_success() &&
                   results["edge_stage"].is_success();
    
    if (success && results["edge_stage"].value.has_value()) {
        try {
            auto output_img = std::any_cast<task_graph::Image>(results["edge_stage"].value);
            success = output_img.width > 0 && output_img.height > 0;
        } catch (...) {
            success = false;
        }
    }
    
    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

int main() {
    std::vector<bool> results;
    
    results.push_back(test_single_blur_filter());
    results.push_back(test_multiple_filter_cascade());
    results.push_back(test_parallel_filters());
    results.push_back(test_long_filter_pipeline());
    results.push_back(test_same_type_multiple_instances());
    results.push_back(test_filter_with_custom_ids());
    results.push_back(test_image_data_passing());
    
    std::cout << "\n--- Summary ---" << std::endl;
    int passed = std::count(results.begin(), results.end(), true);
    std::cout << passed << "/" << results.size() << " tests passed" << std::endl;
    
    return passed == results.size() ? 0 : 1;
}
