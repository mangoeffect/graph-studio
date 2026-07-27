#include <task_graph/data_types.hpp>
#include <task_graph/task_graph.hpp>
#include <task_graph/executor.hpp>
#include <plugin_api.hpp>
#include <iostream>
#include <any>
#include <vector>
#include <task_graph/task_context.hpp>

bool test_image_type() {
    std::cout << "Test: Image type creation and any cast... ";

    task_graph::Image img(640, 480, 3);
    if (!img.valid()) {
        std::cout << "FAILED (image invalid)" << std::endl;
        return false;
    }

    if (img.total_size() != 640 * 480 * 3) {
        std::cout << "FAILED (size mismatch)" << std::endl;
        return false;
    }

    std::any any_img = img;
    auto casted = task_graph::any_cast_safe<task_graph::Image>(any_img);
    if (!casted) {
        std::cout << "FAILED (any_cast failed)" << std::endl;
        return false;
    }

    if (!task_graph::is_image(any_img)) {
        std::cout << "FAILED (is_image check failed)" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_coordinate_types() {
    std::cout << "Test: Coordinate types... ";

    task_graph::Coordinate2D coord2d(10.5, 20.3);
    if (!coord2d.valid()) {
        std::cout << "FAILED (2D coordinate invalid)" << std::endl;
        return false;
    }

    std::any any_2d = coord2d;
    if (!task_graph::is_coordinate2d(any_2d)) {
        std::cout << "FAILED (is_coordinate2d check failed)" << std::endl;
        return false;
    }

    task_graph::Coordinate3D coord3d(1.0, 2.0, 3.0);
    if (!coord3d.valid()) {
        std::cout << "FAILED (3D coordinate invalid)" << std::endl;
        return false;
    }

    std::any any_3d = coord3d;
    if (!task_graph::is_coordinate3d(any_3d)) {
        std::cout << "FAILED (is_coordinate3d check failed)" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_pointcloud_type() {
    std::cout << "Test: PointCloud type... ";

    task_graph::PointCloud cloud(1000);
    if (!cloud.valid()) {
        std::cout << "FAILED (pointcloud invalid)" << std::endl;
        return false;
    }

    if (cloud.size() != 1000) {
        std::cout << "FAILED (point count mismatch)" << std::endl;
        return false;
    }

    cloud[0] = task_graph::Point(1.0f, 2.0f, 3.0f, 0.8f);
    if (cloud[0].x != 1.0f || cloud[0].intensity != 0.8f) {
        std::cout << "FAILED (point data mismatch)" << std::endl;
        return false;
    }

    std::any any_cloud = cloud;
    if (!task_graph::is_pointcloud(any_cloud)) {
        std::cout << "FAILED (is_pointcloud check failed)" << std::endl;
        return false;
    }

    auto casted = task_graph::any_cast_safe<task_graph::PointCloud>(any_cloud);
    if (!casted || casted->size() != 1000) {
        std::cout << "FAILED (any_cast for pointcloud failed)" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

class ImageProcessingTask : public task_graph::Task {
public:
    ImageProcessingTask(const std::string& id)
        : Task(id, [this](auto& ctx) { return execute_impl(ctx); }, create_config()) {}

    std::vector<task_graph::PortSpec> input_specs() const override {
        return {task_graph::make_port<task_graph::Image>("in")};
    }

private:
    static task_graph::TaskConfig create_config() {
        task_graph::TaskConfig cfg;
        cfg.dependencies = {"source_image"};
        return cfg;
    }

    task_graph::TaskResult execute_impl(task_graph::TaskContext& ctx) {
        auto img_opt = ctx.template get_result_value<task_graph::Image>("source_image");
        if (!img_opt) {
            return task_graph::TaskResult{.status = task_graph::TaskStatus::FAILED};
        }

        task_graph::Image result(img_opt->width, img_opt->height, img_opt->channels);
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = result};
    }
};

bool test_check_input_with_image() {
    std::cout << "Test: check_input with Image type... ";

    task_graph::DAG dag;

    auto producer = std::make_shared<task_graph::Task>("source_image", [](auto& ctx) {
        task_graph::Image img(640, 480, 3);
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = img};
    });

    auto processor = std::make_shared<ImageProcessingTask>("processor");

    dag.add_task(producer);
    dag.add_task(processor);
    dag.add_dependency("source_image", "processor");

    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    if (results["processor"].status != task_graph::TaskStatus::COMPLETED) {
        std::cout << "FAILED (processor should be completed)" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_check_input_wrong_type() {
    std::cout << "Test: check_input rejects wrong type... ";

    task_graph::DAG dag;

    auto producer = std::make_shared<task_graph::Task>("source_image", [](auto& ctx) {
        return task_graph::TaskResult{.status = task_graph::TaskStatus::COMPLETED, .value = std::string("not_an_image")};
    });

    auto processor = std::make_shared<ImageProcessingTask>("processor");

    dag.add_task(producer);
    dag.add_task(processor);
    dag.add_dependency("source_image", "processor");

    task_graph::DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    if (results["processor"].status != task_graph::TaskStatus::FAILED) {
        std::cout << "FAILED (processor should be failed)" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

int main() {
    std::cout << "=== Data Types Tests ===\n" << std::endl;

    bool all_passed = true;
    all_passed &= test_image_type();
    all_passed &= test_coordinate_types();
    all_passed &= test_pointcloud_type();
    all_passed &= test_check_input_with_image();
    all_passed &= test_check_input_wrong_type();

    std::cout << "\n=== All tests " << (all_passed ? "PASSED" : "FAILED") << " ===" << std::endl;
    return all_passed ? 0 : 1;
}
