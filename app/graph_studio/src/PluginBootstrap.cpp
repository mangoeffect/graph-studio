#include "PluginBootstrap.h"

#include <task_graph_api.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSet>
#include <unordered_map>

namespace graph_studio {

namespace {

// 进程生命期持有的加载器：unload 在析构时统一进行
task_graph::PluginLoader& sharedLoader() {
    static task_graph::PluginLoader loader;
    return loader;
}

// 收集目录下的插件文件（*.dylib / *.so / *.dll）。
// 多配置生成器（MSVC 的 Debug/Release）会把产物放到 <plugin>/<Config>/ 子目录，
// 因此顶层为空时再扫一级子目录，兼容 macOS/Linux 的顶层直出布局。
// preferConfig：当前 exe 所在的构建配置目录名（Debug/RelWithDebInfo/...）。
// 有匹配子目录时只收集该配置——Debug 与 RelWithDebInfo 产物混装进同一进程会
// 跨 CRT 配置传递堆对象（堆损坏），且异配置 DLL 初始化失败后还会毒化加载器，
// 导致后续本可正常加载的插件全部 LoadLibrary 失败。
static QStringList collectPluginFiles(const QDir& dir, const QString& preferConfig = QString()) {
    QStringList files;
    if (!dir.exists()) return files;
    const QStringList filters{"*.dylib", "*.so", "*.dll"};
    for (const auto& info : dir.entryInfoList(filters, QDir::Files)) {
        files.append(info.absoluteFilePath());
    }
    if (files.isEmpty()) {
        const auto allSubs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        QFileInfoList subs = allSubs;
        if (!preferConfig.isEmpty()) {
            QFileInfoList sameConfig;
            for (const auto& sub : allSubs) {
                if (sub.fileName() == preferConfig) sameConfig.append(sub);
            }
            if (!sameConfig.isEmpty()) subs = sameConfig;
        }
        for (const auto& sub : subs) {
            // 跳过备份/历史目录（如切换 Config 后遗留的 RelWithDebInfo.bak）：
            // 其中是另一 CRT 配置（release）的产物，误装入 debug 进程会跨 CRT
            // 传递堆对象 → 堆损坏崩溃。仅扫正式的 <Config> 子目录。
            if (sub.fileName().endsWith(".bak")) continue;
            QDir cfg(sub.absoluteFilePath());
            for (const auto& f : cfg.entryInfoList(filters, QDir::Files)) {
                files.append(f.absoluteFilePath());
            }
        }
    }
    return files;
}

// 反推仓库根目录：
//   macOS .app: .../app/graph_studio/build/graph_studio.app/Contents/MacOS/exe
//      -> 上溯到 build/，再上溯到 graph_studio/，再上溯到 app/，再上溯到 root
//   其它平台：  .../app/graph_studio/build/exe -> 上溯到 build/ -> .../ -> root
// 启发式：从 exe 向上找，遇到名字叫 "build" 或 "graph_studio" 的目录停止上溯一层。
QString deduceRepoRoot() {
    QString exeDir = QCoreApplication::applicationDirPath();
    QDir d(exeDir);
    // macOS bundle：Contents/MacOS -> Contents -> graph_studio.app -> build
    if (d.dirName() == "MacOS") d.cdUp();          // -> Contents
    if (d.dirName() == "Contents") d.cdUp();        // -> graph_studio.app
    // 现在 d 应该是 build/ 或包含 exe 的目录
    QString buildDir = d.absolutePath();            // 期望是 .../build 或 .../build_wasm
    // 上溯查找：找到包含 subnode.json 的目录即 root
    QDir cur(buildDir);
    for (int i = 0; i < 6 && !cur.isRoot(); ++i) {
        if (QFileInfo(cur.absoluteFilePath("subnode.json")).exists()) {
            return cur.absolutePath();
        }
        cur.cdUp();
    }
    return QString();
}

}  // namespace

PluginLoadResult LoadBuiltinPlugins()
{
    PluginLoadResult result;

#ifdef __EMSCRIPTEN__
    // WASM：插件已通过 subnode __attribute__((constructor)) 静态注册（构建期
    // add_subdirectory + target_link_libraries 链入主 wasm）。不做 dlopen
    // （emscripten 单/多线程 dlopen 都受限，且与 ASYNCIFY/pthread 兼容性差）。
    // 仅枚举当前 registry 内容，方便 UI 启动期日志确认。
    for (const auto& name : task_graph::PluginRegistry::instance().available_tasks()) {
        result.loaded.append(QString::fromStdString(name));
    }
    TG_LOG_INFO(("WASM: " + std::to_string(result.loaded.size()) +
                 " statically-registered tasks available").c_str());
    return result;
#else
    QStringList candidates;
    QSet<QString> seen;

    auto addCandidate = [&](const QString& path) {
        if (!path.isEmpty() && !seen.contains(path)) {
            seen.insert(path);
            candidates.append(path);
        }
    };

    // 1) 环境变量 TASK_GRAPH_PLUGINS_PATH
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString envPaths = env.value("TASK_GRAPH_PLUGINS_PATH");
    if (!envPaths.isEmpty()) {
        // 平台相关分隔符：Windows ';' / macOS/Linux ':'。用 QDir::listSeparator()
        // 避免误伤 Windows 盘符后的冒号（"F:\..." 被 "[:;]" 切成 "F" + "\..."）。
        for (const auto& p : envPaths.split(QDir::listSeparator())) {
            addCandidate(p.trimmed());
        }
    }

    // 2) 开发期路径：<root>/build/submodules/*
    //    只收集与 exe 同构建配置的插件产物（见 collectPluginFiles）。
    const QString exeConfig = QDir(QCoreApplication::applicationDirPath()).dirName();
    const QString root = deduceRepoRoot();
    if (!root.isEmpty()) {
        const QDir submodsDir(root + "/build/submodules");
        if (submodsDir.exists()) {
            for (const auto& sub : submodsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                QDir subDir(submodsDir.absoluteFilePath(sub));
                for (const auto& f : collectPluginFiles(subDir, exeConfig)) addCandidate(f);
            }
        }
    }

    // 3) 打包布局内的插件目录：
    //    macOS .app bundle → <bundle>/Contents/PlugIns（exe 相对 ../PlugIns）
    //    Windows MSIX     → <package root>/PlugIns（exe 同目录）
    //    两种相对路径都探测（不存在的目录由 collectPluginFiles 直接跳过），
    //    便于同一套安装程序模板跨平台复用。
    for (const auto& rel : {"../PlugIns", "PlugIns"}) {
        QDir pluginsDir(QCoreApplication::applicationDirPath() + "/" + rel);
        for (const auto& f : collectPluginFiles(pluginsDir)) addCandidate(f);
    }

    // 逐个 dlopen
    auto& loader = sharedLoader();
    for (const QString& path : candidates) {
        QFileInfo fi(path);
        if (!fi.exists()) {
            result.failed.append(path + " (missing)");
            continue;
        }
        if (loader.load(path.toStdString())) {
            result.loaded.append(path);
            TG_LOG_INFO("Plugin loaded: " + path.toStdString());
        } else {
            result.failed.append(path);
            TG_LOG_WARN("Plugin load FAILED: " + path.toStdString());
        }
    }

    // 汇总日志（在 MainWindow 的 log panel 显示）
    TG_LOG_INFO(("Plugins: " + std::to_string(result.loaded.size()) + " loaded, " +
                 std::to_string(result.failed.size()) + " failed").c_str());

    return result;
#endif  // __EMSCRIPTEN__
}

}  // namespace graph_studio
