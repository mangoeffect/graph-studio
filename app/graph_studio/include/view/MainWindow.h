#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QToolBar>
#include <QDockWidget>
#include <QStatusBar>
#include <QGraphicsView>

namespace graph_studio {

class GraphScene;
class GraphViewModel;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(GraphViewModel& vm, QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void InitializeLayout();
    void CreateToolbar();
    void CreateSidebar();
    void CreateCanvas();
    void CreateStatusBar();

    GraphViewModel& vm_;
    QToolBar* toolbar_ = nullptr;
    QDockWidget* sidebar_ = nullptr;
    QGraphicsView* graphicsView_ = nullptr;
    GraphScene* scene_ = nullptr;
    QStatusBar* statusBar_ = nullptr;
};

} // namespace graph_studio

#endif // MAIN_WINDOW_H