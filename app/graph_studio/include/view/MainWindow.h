#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include <QToolBar>
#include <QStatusBar>
#include <QGraphicsView>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QLabel>
#include <QFormLayout>
#include <QGroupBox>

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
    void ApplyDarkTheme();
    void CreateMenuBar();
    void CreateToolbar();
    QWidget* CreateTaskPanel();
    QWidget* CreateImageResultPanel();
    QWidget* CreateNodePropertyPanel();
    QWidget* CreateLogPanel();
    QWidget* CreateOutputPanel();
    void CreateCanvas();
    void CreateStatusBar();
    void PopulateTaskLibrary();

    GraphViewModel& vm_;
    QSplitter* mainSplitter_ = nullptr;
    QSplitter* topSplitter_ = nullptr;
    QSplitter* bottomSplitter_ = nullptr;
    QToolBar* toolbar_ = nullptr;
    QGraphicsView* graphicsView_ = nullptr;
    GraphScene* scene_ = nullptr;
    QStatusBar* statusBar_ = nullptr;

    QListWidget* taskList_ = nullptr;
    QLabel* imageResultLabel_ = nullptr;
    QFormLayout* nodePropertyLayout_ = nullptr;
    QGroupBox* nodePropertyGroup_ = nullptr;
    QPlainTextEdit* logWidget_ = nullptr;
    QPlainTextEdit* outputWidget_ = nullptr;
};

} // namespace graph_studio

#endif // MAIN_WINDOW_H