// WASM MEMFS 线程验证：ImageReadTask 在 worker 线程通过 executor 执行时，
// 能否从 MEMFS 路径 imread 成功。
#include <task_graph/task_graph.hpp>
#include <task_graph/task_context.hpp>
#include <plugin_api.hpp>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <thread>

int main() {
    using namespace task_graph;

    // 1) 在 MEMFS 写一张测试图（用 OpenCV 生成并 imwrite）
    const std::string path = "/tmp/_verify_input.png";
    cv::Mat src(64, 48, CV_8UC3, cv::Scalar(10, 20, 30));
    if (!cv::imwrite(path, src)) {
        std::cerr << "FAIL: imwrite to MEMFS failed\n";
        return 1;
    }
    std::cout << "wrote test image to MEMFS: " << path << "\n";

    // 2) 通过 registry 确认 image_reader task 已静态注册
    auto& reg = PluginRegistry::instance();
    if (!reg.has_task("opencv_image_read")) {
        std::cerr << "FAIL: opencv_image_read not registered\n";
        return 1;
    }

    // 3) 构造带 file_path 参数的 TaskConfig
    TaskConfig config;
    config.params.set_string("file_path", path);

    // 4) 用 DAG + executor 跑（WASM multithread 下 task 在 worker 线程 execute）
    DAG dag;
    dag.add_plugin_task("reader", "opencv_image_read", config);

    DAGExecutor executor;
    auto fut = executor.execute(dag);
    fut.wait();

    auto results = executor.get_results();
    auto it = results.find("reader");
    if (it == results.end() || it->second.status != TaskStatus::COMPLETED) {
        std::cerr << "FAIL: task did not complete (worker thread MEMFS access?)\n";
        return 1;
    }
    const TaskResult& result = it->second;

    // 5) 校验输出 cv::Mat
    try {
        auto mat = std::any_cast<cv::Mat>(result.value);
        if (mat.empty() || mat.cols != 48 || mat.rows != 64) {
            std::cerr << "FAIL: output mat wrong size: "
                      << mat.cols << "x" << mat.rows << "\n";
            return 1;
        }
        std::cout << "PASS: worker-thread imread from MEMFS OK, mat="
                  << mat.cols << "x" << mat.rows << " ch=" << mat.channels() << "\n";
    } catch (const std::bad_any_cast&) {
        std::cerr << "FAIL: output value is not cv::Mat\n";
        return 1;
    }

    return 0;
}
