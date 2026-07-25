#include <task_graph/task_graph.hpp>
#include <task_graph/dag_serializer.hpp>
#include <task_graph/data_types.hpp>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <task_graph/task_context.hpp>

bool test_single_opencv_blur_filter() {
    std::cout << "Test: Single blur filter execution... ";
    
    task_graph::DAG dag;
    
    auto input_task = std::make_shared<task_graph::Task>("input_image", [](task_graph::TaskContext& ctx) {
        cv::Mat mat = cv::Mat::ones(100, 100, CV_8UC3) * 200;
        task_graph::Image img = task_graph::Image::from_mat(mat);
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = img};
    });
    
    dag.add_task(input_task);
    dag.add_plugin_task("my_blur", "opencv_blur_filter");
    
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
    
    auto input_task = std::make_shared<task_graph::Task>("input_image", [](task_graph::TaskContext& ctx) {
        cv::Mat mat = cv::Mat::ones(100, 100, CV_8UC3) * 200;
        task_graph::Image img = task_graph::Image::from_mat(mat);
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = img};
    });
    
    dag.add_task(input_task);
    dag.add_plugin_task("gaussian_blur", "opencv_gaussian_blur_filter");
    dag.add_plugin_task("sobel_edge", "opencv_sobel_filter");
    
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
    
    auto input_task = std::make_shared<task_graph::Task>("input_image", [](task_graph::TaskContext& ctx) {
        cv::Mat mat = cv::Mat::ones(100, 100, CV_8UC3) * 200;
        task_graph::Image img = task_graph::Image::from_mat(mat);
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = img};
    });
    
    dag.add_task(input_task);
    dag.add_plugin_task("box_blur", "opencv_blur_filter");
    dag.add_plugin_task("median_blur", "opencv_median_blur_filter");
    dag.add_plugin_task("bilateral", "opencv_bilateral_filter");
    
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
    
    auto input_task = std::make_shared<task_graph::Task>("input_image", [](task_graph::TaskContext& ctx) {
        cv::Mat mat = cv::Mat::ones(200, 200, CV_8UC3) * 150;
        task_graph::Image img = task_graph::Image::from_mat(mat);
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = img};
    });
    
    dag.add_task(input_task);
    dag.add_plugin_task("stage1_box", "opencv_box_filter");
    dag.add_plugin_task("stage2_gaussian", "opencv_gaussian_blur_filter");
    dag.add_plugin_task("stage3_median", "opencv_median_blur_filter");
    dag.add_plugin_task("stage4_sobel", "opencv_sobel_filter");
    dag.add_plugin_task("stage5_laplacian", "opencv_laplacian_filter");
    
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
    
    auto input_task = std::make_shared<task_graph::Task>("input_image", [](task_graph::TaskContext& ctx) {
        cv::Mat mat = cv::Mat::ones(100, 100, CV_8UC3) * 200;
        task_graph::Image img = task_graph::Image::from_mat(mat);
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = img};
    });
    
    dag.add_task(input_task);
    dag.add_plugin_task("blur_instance_1", "opencv_blur_filter");
    dag.add_plugin_task("blur_instance_2", "opencv_blur_filter");
    dag.add_plugin_task("blur_instance_3", "opencv_blur_filter");
    
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
    
    auto input_task = std::make_shared<task_graph::Task>("input_image", [](task_graph::TaskContext& ctx) {
        cv::Mat mat = cv::Mat::ones(100, 100, CV_8UC3) * 150;
        task_graph::Image img = task_graph::Image::from_mat(mat);
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = img};
    });
    dag.add_task(input_task);
    
    dag.add_plugin_task("preprocessing_blur", "opencv_gaussian_blur_filter");
    dag.add_plugin_task("edge_detection", "opencv_sobel_filter");
    dag.add_plugin_task("enhancement", "opencv_laplacian_filter");
    
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
    
    auto input_task = std::make_shared<task_graph::Task>("input_image", [](task_graph::TaskContext& ctx) {
        cv::Mat mat = cv::Mat::zeros(100, 100, CV_8UC3);
        cv::rectangle(mat, cv::Rect(20, 20, 60, 60), cv::Scalar(255, 128, 64), -1);
        task_graph::Image img = task_graph::Image::from_mat(mat);
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = img};
    });
    
    dag.add_task(input_task);
    dag.add_plugin_task("blur_stage", "opencv_blur_filter");
    dag.add_plugin_task("edge_stage", "opencv_sobel_filter");
    
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
            auto output_mat = std::any_cast<cv::Mat>(results["edge_stage"].value);
            success = !output_mat.empty();
        } catch (...) {
            success = false;
        }
    }
    
    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

bool test_filter_with_params() {
    std::cout << "Test: Filter with custom params (kernel_size)... ";
    
    task_graph::DAG dag;
    
    auto input_task = std::make_shared<task_graph::Task>("input_image", [](task_graph::TaskContext& ctx) {
        cv::Mat mat = cv::Mat::ones(100, 100, CV_8UC3) * 200;
        task_graph::Image img = task_graph::Image::from_mat(mat);
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = img};
    });
    dag.add_task(input_task);
    
    task_graph::TaskConfig blur_config;
    blur_config.params.set_int("kernel_size", 9);
    
    auto blur_task = std::make_shared<task_graph::Task>(
        "custom_blur",
        "opencv_blur_filter",
        [](task_graph::TaskContext& ctx) {
            auto img_opt = ctx.template get_result_value<task_graph::Image>("input_image");
            if (!img_opt) {
                return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
            }
            
            int kernel_size = ctx.get_param_int("kernel_size").value_or(5);
            if (kernel_size % 2 == 0) kernel_size++;
            if (kernel_size < 1) kernel_size = 1;
            
            cv::Mat mat = img_opt->to_mat();
            cv::Mat result;
            cv::blur(mat, result, cv::Size(kernel_size, kernel_size));
            
            task_graph::Image output = task_graph::Image::from_mat(result);
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = output};
        },
        blur_config
    );
    dag.add_task(blur_task);
    
    dag.add_dependency("input_image", "custom_blur");
    
    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();
    
    auto results = executor.get_results();
    
    bool success = results["input_image"].is_success() && 
                   results["custom_blur"].is_success();
    
    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

bool test_gaussian_blur_with_params() {
    std::cout << "Test: GaussianBlur with kernel_size and sigma params... ";
    
    task_graph::DAG dag;
    
    auto input_task = std::make_shared<task_graph::Task>("input_image", [](task_graph::TaskContext& ctx) {
        cv::Mat mat = cv::Mat::ones(100, 100, CV_8UC3) * 200;
        task_graph::Image img = task_graph::Image::from_mat(mat);
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = img};
    });
    dag.add_task(input_task);
    
    task_graph::TaskConfig gaussian_config;
    gaussian_config.params.set_int("kernel_size", 7);
    gaussian_config.params.set_float("sigma", 2.0f);
    
    auto gaussian_task = std::make_shared<task_graph::Task>(
        "gaussian_custom",
        "opencv_gaussian_blur_filter",
        [](task_graph::TaskContext& ctx) {
            auto img_opt = ctx.template get_result_value<task_graph::Image>("input_image");
            if (!img_opt) {
                return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
            }
            
            int kernel_size = ctx.get_param_int("kernel_size").value_or(5);
            if (kernel_size % 2 == 0) kernel_size++;
            if (kernel_size < 1) kernel_size = 1;
            
            double sigma = ctx.get_param_float("sigma").value_or(0.0);
            
            cv::Mat mat = img_opt->to_mat();
            cv::Mat result;
            cv::GaussianBlur(mat, result, cv::Size(kernel_size, kernel_size), sigma);
            
            task_graph::Image output = task_graph::Image::from_mat(result);
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = output};
        },
        gaussian_config
    );
    dag.add_task(gaussian_task);
    
    dag.add_dependency("input_image", "gaussian_custom");
    
    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();
    
    auto results = executor.get_results();
    
    bool success = results["input_image"].is_success() && 
                   results["gaussian_custom"].is_success();
    
    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

bool test_cascade_with_params() {
    std::cout << "Test: Cascade filters with params... ";
    
    task_graph::DAG dag;
    
    auto input_task = std::make_shared<task_graph::Task>("input_image", [](task_graph::TaskContext& ctx) {
        cv::Mat mat = cv::Mat::ones(100, 100, CV_8UC3) * 200;
        task_graph::Image img = task_graph::Image::from_mat(mat);
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = img};
    });
    dag.add_task(input_task);
    
    task_graph::TaskConfig gaussian_config;
    gaussian_config.params.set_int("kernel_size", 5);
    gaussian_config.params.set_float("sigma", 1.5f);
    
    auto gaussian_task = std::make_shared<task_graph::Task>(
        "gaussian_stage",
        "opencv_gaussian_blur_filter",
        [](task_graph::TaskContext& ctx) {
            auto img_opt = ctx.template get_result_value<task_graph::Image>("input_image");
            if (!img_opt) {
                return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
            }
            
            int kernel_size = ctx.get_param_int("kernel_size").value_or(5);
            if (kernel_size % 2 == 0) kernel_size++;
            if (kernel_size < 1) kernel_size = 1;
            
            double sigma = ctx.get_param_float("sigma").value_or(0.0);
            
            cv::Mat mat = img_opt->to_mat();
            cv::Mat result;
            cv::GaussianBlur(mat, result, cv::Size(kernel_size, kernel_size), sigma);
            
            task_graph::Image output = task_graph::Image::from_mat(result);
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = output};
        },
        gaussian_config
    );
    dag.add_task(gaussian_task);
    
    task_graph::TaskConfig sobel_config;
    sobel_config.params.set_int("kernel_size", 5);
    
    auto sobel_task = std::make_shared<task_graph::Task>(
        "sobel_stage",
        "opencv_sobel_filter",
        [](task_graph::TaskContext& ctx) {
            auto img_opt = ctx.template get_result_value<task_graph::Image>("gaussian_stage");
            if (!img_opt) {
                return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
            }
            
            int kernel_size = ctx.get_param_int("kernel_size").value_or(3);
            if (kernel_size % 2 == 0) kernel_size++;
            if (kernel_size < 1) kernel_size = 1;
            if (kernel_size > 7) kernel_size = 7;
            
            cv::Mat mat = img_opt->to_mat();
            cv::Mat gray, grad_x, grad_y, abs_grad_x, abs_grad_y, result;
            
            cv::cvtColor(mat, gray, cv::COLOR_BGR2GRAY);
            cv::Sobel(gray, grad_x, CV_16S, 1, 0, kernel_size);
            cv::Sobel(gray, grad_y, CV_16S, 0, 1, kernel_size);
            
            cv::convertScaleAbs(grad_x, abs_grad_x);
            cv::convertScaleAbs(grad_y, abs_grad_y);
            
            cv::addWeighted(abs_grad_x, 0.5, abs_grad_y, 0.5, 0, result);
            
            task_graph::Image output = task_graph::Image::from_mat(result);
            return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = output};
        },
        sobel_config
    );
    dag.add_task(sobel_task);
    
    dag.add_dependency("input_image", "gaussian_stage");
    dag.add_dependency("gaussian_stage", "sobel_stage");
    
    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();
    
    auto results = executor.get_results();
    
    bool success = results["input_image"].is_success() && 
                   results["gaussian_stage"].is_success() &&
                   results["sobel_stage"].is_success();
    
    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

bool test_params_from_json() {
    std::cout << "Test: Filter params from JSON... ";
    
    std::string json_str = R"(
        {
            "version": "1.0",
            "tasks": [
                {"id": "input_image"},
                {
                    "id": "param_blur",
                    "type": "opencv_blur_filter",
                    "params": {
                        "kernel_size": 9
                    }
                },
                {
                    "id": "param_sobel",
                    "type": "opencv_sobel_filter",
                    "params": {
                        "kernel_size": 5,
                        "dx": 1,
                        "dy": 1
                    }
                }
            ],
            "edges": [
                {"from": "input_image", "to": "param_blur"},
                {"from": "param_blur", "to": "param_sobel"}
            ]
        }
    )";
    
    task_graph::DAG dag = task_graph::DAGSerializer::from_string(json_str);
    
    auto input_task = std::make_shared<task_graph::Task>("input_image", [](task_graph::TaskContext& ctx) {
        cv::Mat mat = cv::Mat::ones(100, 100, CV_8UC3) * 200;
        task_graph::Image img = task_graph::Image::from_mat(mat);
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = img};
    });
    dag.replace_task("input_image", input_task);
    
    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();
    
    auto results = executor.get_results();
    
    bool success = results["input_image"].is_success() && 
                   results["param_blur"].is_success() &&
                   results["param_sobel"].is_success();
    
    std::cout << (success ? "PASSED" : "FAILED") << std::endl;
    return success;
}

int main() {
    std::vector<bool> results;
    
    results.push_back(test_single_opencv_blur_filter());
    results.push_back(test_multiple_filter_cascade());
    results.push_back(test_parallel_filters());
    results.push_back(test_long_filter_pipeline());
    results.push_back(test_same_type_multiple_instances());
    results.push_back(test_filter_with_custom_ids());
    results.push_back(test_image_data_passing());
    results.push_back(test_filter_with_params());
    results.push_back(test_gaussian_blur_with_params());
    results.push_back(test_cascade_with_params());
    results.push_back(test_params_from_json());
    
    std::cout << "\n--- Summary ---" << std::endl;
    int passed = std::count(results.begin(), results.end(), true);
    std::cout << passed << "/" << results.size() << " tests passed" << std::endl;
    
    return passed == results.size() ? 0 : 1;
}
