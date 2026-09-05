// MNN 推理任务端到端测试。模型/标签由 scripts/download_mnn_models.py 产出，
// 缺失时软跳过（对齐 mediapipe 子节点惯例）。
// 链路：合成 Image → LambdaNode 源 → mnn_inference / mnn_image_classifier，
// 经 DAGExecutor 真实执行，断言输出张量 shape 与 top-k 结构。
#include <gtest/gtest.h>

#include <plugin_api.hpp>
#include <task_graph/dag.hpp>
#include <task_graph/data_types.hpp>
#include <task_graph/executor.hpp>
#include <task_graph/mnn/mnn_inference_task.hpp>
#include <task_graph/task.hpp>

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace task_graph;

#ifndef TG_TEST_MNN_MODELS_DIR
#define TG_TEST_MNN_MODELS_DIR "./tests/models/mnn"
#endif

namespace {

const std::filesystem::path kModelsDir = TG_TEST_MNN_MODELS_DIR;
const std::filesystem::path kModelPath = kModelsDir / "mobilenet_v2.mnn";
const std::filesystem::path kLabelsPath = kModelsDir / "labels.txt";

bool assets_ready() {
    std::error_code ec;
    return std::filesystem::is_regular_file(kModelPath, ec) &&
           std::filesystem::is_regular_file(kLabelsPath, ec);
}

// 确定性合成图（64x64 RGB 渐变）——刻意小于 224 输入，覆盖预处理缩放路径
Image make_source_image() {
    constexpr int kW = 64, kH = 64;
    Image img(kW, kH, 3);
    img.pixel_format = PixelFormat::BGR;  // 通道序语义由任务 source_format 决定
    img.data_type = DataType::UINT8;
    auto& data = *img.data;
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const size_t i = (static_cast<size_t>(y) * kW + x) * 3;
            data[i + 0] = static_cast<uint8_t>((x * 4) % 256);
            data[i + 1] = static_cast<uint8_t>((y * 4) % 256);
            data[i + 2] = static_cast<uint8_t>(((x + y) * 2) % 256);
        }
    }
    return img;
}

// 测试源节点：恒定输出同一张合成图（LambdaNode 无固定端口契约）
class ImageSourceNode : public INode {
public:
    ImageSourceNode(const std::string& id)
        : INode(id), image_(make_source_image()) {}

    const std::string& type() const override {
        static const std::string t("test_image_source");
        return t;
    }
    std::vector<PortSpec> input_specs() const override { return {}; }
    std::vector<PortSpec> output_specs() const override {
        return {make_port<Image>("out")};
    }
    TaskResult execute(TaskContext&) override {
        TaskResult r;
        r.status = TaskStatus::COMPLETED;
        r.value = image_;
        return r;
    }

private:
    Image image_;
};

}  // namespace

TEST(MnnInference, tasks_registered) {
    // 任务注册进 PluginRegistry（核心库内 TG_PLUGIN_AUTOREG + anchor 拉入）
    EXPECT_TRUE(PluginRegistry::instance().has_task("mnn_inference"));
    EXPECT_TRUE(PluginRegistry::instance().has_task("mnn_image_classifier"));

    // 经注册表创建（GraphStudio 面板同路径）
    auto node = PluginRegistry::instance().create_task("mnn_image_classifier");
    ASSERT_TRUE(node);
    EXPECT_EQ(node->type(), "mnn_image_classifier");
}

TEST(MnnInference, port_types_registered) {
    // 跨 SO 稳定名注册（mnn_registry.cpp 的 TG_REGISTER_TYPE）
    EXPECT_FALSE(type_name<MnnInferenceResult>().empty());
    EXPECT_EQ(type_name<MnnClassificationResult>(), "task_graph::MnnClassificationResult");
}

TEST(MnnInference, inference_runs_and_outputs_all_tensors) {
    if (!assets_ready()) {
        GTEST_SKIP() << "[SKIP] MNN 模型缺失，运行 scripts/download_mnn_models.py";
    }

    TaskConfig cfg;
    cfg.params.set_string("model_path", kModelPath.string());
    cfg.params.set_int("device", 0);  // CPU（CI 无 Metal 依赖假设）
    cfg.params.set_int("threads", 2);

    DAG dag;
    dag.add_task(std::make_shared<ImageSourceNode>("src"));
    dag.add_task(std::make_shared<MnnInferenceTask>("mnn", cfg));
    dag.connect("src", "out", "mnn", "in");

    DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    ASSERT_TRUE(results.count("mnn"));
    EXPECT_TRUE(results["mnn"].is_success());

    const auto result = std::any_cast<MnnInferenceResult>(results["mnn"].value);
    ASSERT_FALSE(result.tensors.empty());

    // MobileNetV2-12：唯一输出 [1,1000]（NCHW logits）
    bool found_logits = false;
    for (const auto& t : result.tensors) {
        if (t.shape == std::vector<int>({1, 1000})) {
            found_logits = true;
            ASSERT_EQ(t.data.size(), 1000u);
            for (float v : t.data) {
                EXPECT_TRUE(std::isfinite(v));
            }
        }
    }
    EXPECT_TRUE(found_logits);
}

TEST(MnnInference, classifier_topk_and_labels) {
    if (!assets_ready()) {
        GTEST_SKIP() << "[SKIP] MNN 模型缺失，运行 scripts/download_mnn_models.py";
    }

    TaskConfig cfg;
    cfg.params.set_string("model_path", kModelPath.string());
    cfg.params.set_string("labels_path", kLabelsPath.string());
    cfg.params.set_int("device", 0);
    cfg.params.set_int("top_k", 3);

    DAG dag;
    dag.add_task(std::make_shared<ImageSourceNode>("src"));
    dag.add_task(std::make_shared<MnnImageClassifierTask>("clf", cfg));
    dag.connect("src", "out", "clf", "in");

    DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    ASSERT_TRUE(results.count("clf"));
    EXPECT_TRUE(results["clf"].is_success());

    const auto result = std::any_cast<MnnClassificationResult>(results["clf"].value);
    ASSERT_EQ(result.categories.size(), 3u);

    // score 降序、索引合法、标签来自文件（synset.txt 首类含 "tench"）
    for (size_t i = 1; i < result.categories.size(); ++i) {
        EXPECT_GE(result.categories[i - 1].score, result.categories[i].score - 1e-6f)
            << "top-k 未按 score 降序";
    }
    for (const auto& c : result.categories) {
        EXPECT_GE(c.index, 0);
        EXPECT_LT(c.index, 1000);
        EXPECT_FALSE(c.label.empty());
    }
}

TEST(MnnInference, missing_model_fails_with_readable_error) {
    if (assets_ready()) {
        GTEST_SKIP() << "[SKIP] 本用例验证模型缺失路径，仅在有引擎无模型环境有意义";
    }

    TaskConfig cfg;
    cfg.params.set_string("model_path", "no_such_model.mnn");

    DAG dag;
    dag.add_task(std::make_shared<ImageSourceNode>("src"));
    dag.add_task(std::make_shared<MnnInferenceTask>("mnn", cfg));
    dag.connect("src", "out", "mnn", "in");

    DAGExecutor executor;
    executor.execute(dag).wait();

    auto results = executor.get_results();
    ASSERT_TRUE(results.count("mnn"));
    EXPECT_TRUE(results["mnn"].is_failed());
}
