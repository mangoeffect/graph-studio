#include <task_graph/mnn/mnn_inference_task.hpp>

#include <memory>
#include <string>

#include <plugin_api.hpp>

namespace task_graph {

namespace {

// 类型名常量（anonymous namespace，避免静态初始化顺序问题——AGENTS.md 约定）
const char* const kMnnInferenceType = "mnn_inference";
const char* const kMnnImageClassifierType = "mnn_image_classifier";

void do_register() {
    auto& reg = PluginRegistry::instance();
    reg.register_task(kMnnInferenceType,
                      [](const std::string& id, const TaskConfig& cfg) -> NodePtr {
                          return std::make_shared<MnnInferenceTask>(id, cfg);
                      });
    reg.register_task(kMnnImageClassifierType,
                      [](const std::string& id, const TaskConfig& cfg) -> NodePtr {
                          return std::make_shared<MnnImageClassifierTask>(id, cfg);
                      });
}

void do_unregister() {
    auto& reg = PluginRegistry::instance();
    reg.unregister_task(kMnnInferenceType);
    reg.unregister_task(kMnnImageClassifierType);
}

}  // namespace

// 非 inline anchor：STATIC 核心 lib（iOS/Android/WASM）下链接器会裁剪没有
// 外部引用的 TU，导致本文件的 TG_REGISTER_TYPE / TG_PLUGIN_AUTOREG 静态
// 初始化器不执行。data_types.cpp 的 TypeRegistry::instance()（必然被引用）
// 在 TASK_GRAPH_MNN_AVAILABLE 下调用本函数，强制拉入本 TU——与
// data_types.cpp 的 force-link 注释同一手法。
namespace detail {
void pull_mnn_tasks() {}
}  // namespace detail

// 端口数据类型跨 SO 稳定名注册
TG_REGISTER_TYPE(MnnTensorData, "task_graph::MnnTensorData");
TG_REGISTER_TYPE(MnnInferenceResult, "task_graph::MnnInferenceResult");
TG_REGISTER_TYPE(MnnClassificationResult, "task_graph::MnnClassificationResult");

// 核心库内编译：无需 extern "C" register_plugin（那是 dlopen 插件入口，
// WASM whole-archive 下同名符号还会冲突）；TG_PLUGIN_AUTOREG 覆盖
// GCC/Clang（含静态链接）与 MSVC 两条路径。
TG_PLUGIN_AUTOREG(do_register, do_unregister);

}  // namespace task_graph
