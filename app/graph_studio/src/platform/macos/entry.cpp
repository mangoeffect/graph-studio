#include <QApplication>
#include <QMainWindow>
#include <QIcon>
#include <QSize>
#include <QSurfaceFormat>

#include "model/GraphModel.h"
#include "viewmodel/GraphViewModel.h"
#include "view/MainWindow.h"
#include "PluginBootstrap.h"
#include "GpuBootstrap.h"
#include "ModelBootstrap.h"
#include "CrashReporter.h"

#include <cstring>
#include <cstdlib>
#include <cstdio>

using namespace graph_studio;

int main(int argc, char* argv[])
{
#ifndef __EMSCRIPTEN__
    // 启动崩溃上报（Sentry + crashpad）。在最早期调用以覆盖启动期间崩溃；
    // 未构建 sentry（third_party 缺失）或 SENTRY_DSN 未设置时为 no-op。
    InitCrashReporting();

    // --test-crash：人为触发真实崩溃，验证 crashpad minidump 能上报。
    // 用法：SENTRY_DSN=... graph_studio --test-crash
    bool test_crash = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--test-crash") == 0)
            test_crash = true;
    }

    // Request a 3.3 Core profile context for QOpenGLWidget (required on macOS).
    // WASM uses OpenGL ES natively and does not need this.
#endif
#ifndef __EMSCRIPTEN__
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(fmt);
#endif

    QApplication app(argc, argv);

#ifndef __EMSCRIPTEN__
    if (test_crash)
        TriggerTestCrash();
#endif

    QIcon appIcon;
    appIcon.addFile(":/icons/app_icon_16.png", QSize(16, 16));
    appIcon.addFile(":/icons/app_icon_32.png", QSize(32, 32));
    appIcon.addFile(":/icons/app_icon_48.png", QSize(48, 48));
    appIcon.addFile(":/icons/app_icon_64.png", QSize(64, 64));
    appIcon.addFile(":/icons/app_icon_128.png", QSize(128, 128));
    appIcon.addFile(":/icons/app_icon_256.png", QSize(256, 256));
    appIcon.addFile(":/icons/app_icon_512.png", QSize(512, 512));
    app.setWindowIcon(appIcon);

    GraphModel model;
    GraphViewModel vm(model);

    // 启动期加载内置插件（subnode 构建产物 + env + .app/PlugIns），
    // 让任务库面板/右键菜单能拿到完整可用 task 列表。
    // 放在 ViewModel 之后：此时日志 sink 已注册，插件加载日志能进 log 面板。
    LoadBuiltinPlugins();

    // 初始化 GPU backend（macOS->Metal），让 gpu_* 节点可用。
    // fail-open：init 失败仅记 WARN，非 GPU 节点仍可执行。
    InitGpuBackend();

    // 安装全局 ModelFinder：任务参数里只填模型名，这里从 models 目录
    //（env / 打包布局）解析出文件路径。fail-open：无 models 目录时名称
    // 回退图相对路径。需在 QApplication 之后（依赖 applicationDirPath）。
    InitModelFinder();

    MainWindow window(vm);

    window.show();

    int ret = app.exec();

    ShutdownModelFinder();
    ShutdownGpuBackend();
#ifndef __EMSCRIPTEN__
    ShutdownCrashReporting();
#endif
    return ret;
}
