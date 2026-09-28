#include "ModelBootstrap.h"

#include <task_graph_api.hpp>

#include <QCoreApplication>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#if defined(__EMSCRIPTEN__) && defined(TG_APP_ASYNCIFY)
#include <emscripten.h>
#include <chrono>
#include <fstream>
#endif

namespace graph_studio {

namespace {

namespace fs = std::filesystem;

// 模型名可能带或不带扩展名（"face_landmarker" / "face_landmarker.task"），
// 依次尝试这些后缀。
const char* const kModelSuffixes[] = {"", ".task", ".tflite", ".mnn"};

// Init 时一次性收集的查找目录快照，回调按值捕获，查询期无 getenv/IO 目录探测。
std::vector<fs::path> collect_model_dirs() {
    std::vector<fs::path> dirs;

    // 1) 显式环境变量优先（dev 脚本、Linux AppImage 的 AppRun 都从这里注入）。
    //    支持路径列表（':'/';' 分隔，与 os path list 兼容）：dev 模式下各
    //    模型集分居 tests/models/{mediapipe,face,matting}，无需合并 staging。
    if (const char* env = std::getenv("GRAPH_STUDIO_MODELS_DIR"); env && *env) {
        std::string list(env);
        size_t start = 0;
        while (start <= list.size()) {
            const size_t pos = list.find_first_of(":;", start);
            const std::string entry =
                list.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
            if (!entry.empty()) {
                dirs.emplace_back(fs::path(entry));
            }
            if (pos == std::string::npos) break;
            start = pos + 1;
        }
    }

#ifndef __EMSCRIPTEN__
    // 桌面布局推断：Windows MSIX / dev 构建（exe 与 models/ 同级）、
    // macOS .app bundle（Contents/Resources/models）。
    const fs::path exe_dir = QCoreApplication::applicationDirPath().toStdString();
    // 2) Windows MSIX / dev 构建布局：exe 与 models/ 同级
    dirs.emplace_back(exe_dir / "models");
    // 3) macOS .app bundle：Contents/Resources/models
    dirs.emplace_back(exe_dir / ".." / "Resources" / "models");
#else
    // WASM 无真实 exe 目录。启动期只拉 manifest（entry.cpp 启动 EM_ASM，
    // 挂 __gsModelsManifest/__gsModelLoad），模型本体按需延迟加载进 MEMFS
    // 固定目录 /models（见下方 finder 的 ensure_model_fetched 分支），
    // ModelFinder 从这里按名解析。目录尚不存在也照常注册（fail-open：
    // manifest 未就绪 / --skip-models 包时名称回退图相对路径）。
    dirs.emplace_back("/models");
#endif
    return dirs;
}

bool dir_exists(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec) && !ec;
}

#if defined(__EMSCRIPTEN__) && defined(TG_APP_ASYNCIFY)
// 模型名经 MEMFS 交换文件传给 JS（EXPORTED_RUNTIME_METHODS 不含字符串转换
// 工具，见 test_hooks.cpp 头注释）。返回：0=下载中 / 1=已落盘 / 2=下载失败 /
// 3=manifest 不可用或无此模型（调用方立即回退）。后缀变体表与
// kModelSuffixes 保持一致。__gsModelLoad 由 entry.cpp 启动 EM_ASM 挂出。
constexpr const char* kModelReqPath = "/tmp/gs_model_req.txt";

int query_model_download() {
    return EM_ASM_INT({
        var name = '';
        try {
            name = new TextDecoder('utf-8')
                .decode(Module.FS.readFile('/tmp/gs_model_req.txt'));
        } catch (e) { return 3; }
        if (!name) return 3;
        var manifest = window.__gsModelsManifest;
        if (!manifest) return 3;
        var actual = null;
        var suffixes = ['', '.task', '.tflite', '.mnn'];
        for (var i = 0; i < suffixes.length; ++i) {
            if (manifest[name + suffixes[i]]) {
                actual = name + suffixes[i];
                break;
            }
        }
        if (!actual) return 3;
        var st = window.__gsModelState || (window.__gsModelState = {});
        if (st[actual] === 'ok') return 1;
        if (st[actual] === 'err') return 2;
        window.__gsModelLoad(actual).then(function() { st[actual] = 'ok'; },
                                          function() { st[actual] = 'err'; });
        return 0;
    });
}

// 把 manifest 命中的模型按需拉进 MEMFS /models（cache 命中则本地回填）。
// 落盘返回 true；失败/不可用/超时 false（调用方回退图相对路径）。等待期间
// emscripten_sleep 泵事件循环（与 GPU spin_until 同款 Asyncify 模式），UI
// 不卡。仅 Asyncify 构建可用：非 asyncify 下 emscripten_sleep 直接 throw。
bool ensure_model_fetched(const std::string& name) {
    {
        std::ofstream f(kModelReqPath, std::ios::binary | std::ios::trunc);
        f << name;
    }
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(120);
    while (std::chrono::steady_clock::now() < deadline) {
        const int st = query_model_download();
        if (st == 1) return true;
        if (st == 2 || st == 3) return false;
        emscripten_sleep(50);
    }
    return false;
}
#endif

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
    // ModelFinder 契约（任意线程、不得抛异常）。WASM 单线程执行
    //（ThreadPool inline），finder 恒在主线程被调——按需下载分支可安全
    // 使用 emscripten_sleep。
    task_graph::set_model_finder(
        [dirs](const std::string& name) -> std::string {
            if (name.empty()) return {};
            const auto probe = [&dirs, &name]() -> std::string {
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
            };
            std::string hit = probe();
#if defined(__EMSCRIPTEN__) && defined(TG_APP_ASYNCIFY)
            // 按需延迟加载：MEMFS 未命中且 manifest 有此模型时从部署包
            // models/ 拉取（cache 命中本地回填）。首次下载为秒级例外
            //（等待期间泵 UI 事件）；后续命中 MEMFS 即时返回。
            if (hit.empty() && ensure_model_fetched(name)) {
                hit = probe();
            }
#endif
            return hit;
        });
}

void ShutdownModelFinder() {
    task_graph::clear_model_finder();
}

}  // namespace graph_studio
