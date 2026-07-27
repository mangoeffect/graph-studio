#include <QApplication>
#include <QMainWindow>

#include "model/GraphModel.h"
#include "viewmodel/GraphViewModel.h"
#include "view/MainWindow.h"
#include "PluginBootstrap.h"

using namespace graph_studio;

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // 启动期加载内置插件（subnode 构建产物 + env + .app/PlugIns），
    // 让任务库面板/右键菜单能拿到完整可用 task 列表。
    LoadBuiltinPlugins();

    GraphModel model;
    GraphViewModel vm(model);
    MainWindow window(vm);

    window.show();

    return app.exec();
}
