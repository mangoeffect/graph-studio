#ifndef GRAPH_STUDIO_PLUGIN_BOOTSTRAP_H
#define GRAPH_STUDIO_PLUGIN_BOOTSTRAP_H

#include <QString>
#include <QStringList>

namespace graph_studio {

// 内置插件加载结果：成功/失败列表，便于日志输出
struct PluginLoadResult {
    QStringList loaded;
    QStringList failed;
};

// 扫描默认插件路径并 dlopen 加载：
//   1. 环境变量 TASK_GRAPH_PLUGINS_PATH（macOS/Linux 用 ':'，Windows 用 ';'）
//   2. 开发期路径：<repo_root>/build/submodules/*/*.{dylib,so}
//      repo_root 由当前可执行文件位置反推（macOS .app 内部也算）
//   3. macOS .app bundle 内的 PlugIns/ 目录：<exedir>/../PlugIns/*.{dylib,so}
// 单个插件加载失败（如 OpenCV 缺失导致 image_filtering dlopen 失败）仅记入 failed，
// 不影响其它插件与 GUI 启动。PluginLoader 已用 RTLD_NODELETE，进程退出时安全。
PluginLoadResult LoadBuiltinPlugins();

} // namespace graph_studio

#endif // GRAPH_STUDIO_PLUGIN_BOOTSTRAP_H
