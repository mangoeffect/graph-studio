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

using namespace graph_studio;

int main(int argc, char* argv[])
{
    // Request a 3.3 Core profile context for QOpenGLWidget (required on macOS).
    // WASM uses OpenGL ES natively and does not need this.
#ifndef __EMSCRIPTEN__
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(fmt);
#endif

    QApplication app(argc, argv);

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

    MainWindow window(vm);

    window.show();

    int ret = app.exec();

    ShutdownGpuBackend();
    return ret;
}
