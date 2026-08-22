#include "ModelBootstrap.h"

#include <task_graph_api.hpp>

#include <QCoreApplication>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace graph_studio {

namespace {

namespace fs = std::filesystem;

// 模型名可能带或不带扩展名（"face_landmarker" / "face_landmarker.task"），
// 依次尝试这三个后缀。
const char* const kModelSuffixes[] = {"", ".task", ".tflite"};

// Init 时一次性收集的查找目录快照，回调按值捕获，查询期无 getenv/IO 目录探测。
std::vector<fs::path> collect_model_dirs() {
    std::vector<fs::path> dirs;

    // 1) 显式环境变量优先（dev 脚本、Linux AppImage 的 AppRun 都从这里注入）
    if (const char* env = std::getenv("GRAPH_STUDIO_MODELS_DIR"); env && *env) {
        dirs.emplace_back(fs::path(env));
    }

#ifndef __EMSCRIPTEN__
    // WASM 无真实 exe 目录（MEMFS 上传场景），跳过布局推断
    const fs::path exe_dir = QCoreApplication::applicationDirPath().toStdString();
    // 2) Windows MSIX / dev 构建布局：exe 与 models/ 同级
    dirs.emplace_back(exe_dir / "models");
    // 3) macOS .app bundle：Contents/Resources/models
    dirs.emplace_back(exe_dir / ".." / "Resources" / "models");
#endif
    return dirs;
}

bool dir_exists(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec) && !ec;
}

}  // namespace

void InitModelFinder() {
    const std::vector<fs::path> dirs = collect_model_dirs();

    // 记录实际可用的目录（缺目录只降级不报错：名称解析会回退图相对路径）
    std::string found;
    for (const auto& d : dirs) {
        if (dir_exists(d)) {
            if (!found.empty()) found += ", ";
            found += d.lexically_normal().string();
        }
    }
    if (found.empty()) {
        TG_LOG_INFO("ModelFinder: no models directory found; "
                    "model names fall back to graph-relative paths");
    } else {
        TG_LOG_INFO(("ModelFinder: model directories: " + found).c_str());
    }

    // 回调捕获目录快照；仅用不抛重载（error_code 版 exists），满足
    // ModelFinder 契约（任意线程、不得抛异常）。
    task_graph::set_model_finder(
        [dirs](const std::string& name) -> std::string {
            if (name.empty()) return {};
            std::error_code ec;
            for (const auto& dir : dirs) {
                for (const char* suffix : kModelSuffixes) {
                    const fs::path p = dir / (name + suffix);
                    if (fs::is_regular_file(p, ec) && !ec) {
                        return p.lexically_normal().string();
                    }
                }
            }
            return {};
        });
}

void ShutdownModelFinder() {
    task_graph::clear_model_finder();
}

}  // namespace graph_studio
