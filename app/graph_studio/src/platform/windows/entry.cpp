#include <QApplication>

#include "../../model/GraphModel.h"
#include "../../viewmodel/GraphViewModel.h"
#include "../../view/MainWindow.h"

using namespace graph_studio;

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    GraphModel model;
    GraphViewModel vm(model);
    MainWindow window(vm);

    window.show();

    return app.exec();
}