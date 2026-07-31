#include <QApplication>

#include "../../model/GraphModel.h"
#include "../../viewmodel/GraphViewModel.h"
#include "../../view/MainWindow.h"
#include "../../PluginBootstrap.h"

using namespace graph_studio;

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    GraphModel model;
    GraphViewModel vm(model);

    // 启动期加载内置插件（放在 ViewModel 之后：日志 sink 已注册，加载日志能进 log 面板）
    LoadBuiltinPlugins();

    MainWindow window(vm);

    window.show();

    return app.exec();
}