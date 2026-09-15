// js_script 任务注册（核心库内编译，对照 src/mnn/mnn_registry.cpp 手法）。
//
// 无 extern "C" register_plugin：那是 dlopen 插件入口，WASM whole-archive 下
// 同名符号会冲突；核心库注册只需 TG_PLUGIN_AUTOREG（GCC/Clang 含静态链接
// 与 MSVC 两条路径）。
//
// pull_js_tasks()：STATIC（mobile/WASM）构建下无外部引用的注册 TU 会被
// 链接器裁剪；TypeRegistry::instance()（src/data_types.cpp）是全库必经
// 路径，借它 force-link 本 TU。quickjs 是 in-tree 纯 C 源恒可用，故锚点
// 无条件调用（无需 MNN 的 TASK_GRAPH_*_AVAILABLE 门控）。
#include "js_task.hpp"

#include <plugin_api.hpp>

namespace task_graph {
namespace {
const char* const kJsTaskType = "js_script";

bool do_register() {
    PluginRegistry::instance().register_task(
        kJsTaskType,
        [](const std::string& id, const TaskConfig& config) {
            return std::make_shared<JsTask>(id, config);
        });
    return true;
}

void do_unregister() {
    PluginRegistry::instance().unregister_task(kJsTaskType);
}

TG_PLUGIN_AUTOREG(do_register, do_unregister);
}  // namespace

namespace detail {
// TypeRegistry::instance() 引用的 force-link 锚点（定义在本 TU，随
// TG_PLUGIN_AUTOREG 静态初始化器一起被拉入）
void pull_js_tasks() {}
}  // namespace detail

}  // namespace task_graph
