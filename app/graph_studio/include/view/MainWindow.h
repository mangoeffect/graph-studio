#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include <QToolBar>
#include <QStatusBar>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QLabel>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QHash>

#include "view/GraphView.h"
#include "viewmodel/GraphViewModel.h"

namespace graph_studio {

class GraphScene;
class GraphViewModel;
class NodeItem;
class EdgeItem;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(GraphViewModel& vm, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onTaskAdded(const NodeData& node);
    void onTaskRemoved(const QString& taskId);
    void onEdgeAdded(const EdgeData& edge);
    void onEdgeRemoved(const QString& fromId, const QString& toId);
    void onNodeMovedVm(const QString& id, qreal x, qreal y);
    void onGraphReset();
    void onLogMessage(const QString& msg);
    void onSelectionChangedVm(const QString& nodeId);
    void onSceneSelectionChanged();
    void onEdgeCreationRequested(const QString& fromId, const QString& toId);
    void onNodeMovedScene(const QString& id, qreal x, qreal y);
    void onNodeDoubleClicked(const QString& id);

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
    void ConnectSignals();

    void DeleteSelected();
    void CreateNodeAt(const QString& taskType, const QPointF& scenePos);
    void SyncSceneFromViewModel();
    void UpdateStatusBar();
    void UpdatePropertyPanel(const QString& nodeId);
    void ClearPropertyPanel();

    // Actions
    void ActionNew();
    void ActionOpen();
    void ActionSave();
    void ActionSaveAs();
    void ActionAutoLayout();
    void ActionZoomIn();
    void ActionZoomOut();
    void ActionFitToView();

    GraphViewModel& vm_;
    QSplitter* mainSplitter_ = nullptr;
    QSplitter* topSplitter_ = nullptr;
    QSplitter* bottomSplitter_ = nullptr;
    QToolBar* toolbar_ = nullptr;
    GraphView* graphicsView_ = nullptr;
    GraphScene* scene_ = nullptr;
    QStatusBar* statusBar_ = nullptr;

    QListWidget* taskList_ = nullptr;
    QLabel* imageResultLabel_ = nullptr;
    QFormLayout* nodePropertyLayout_ = nullptr;
    QGroupBox* nodePropertyGroup_ = nullptr;
    QPlainTextEdit* logWidget_ = nullptr;
    QPlainTextEdit* outputWidget_ = nullptr;
    QLabel* zoomLabel_ = nullptr;

    // Property panel widgets
    QLineEdit* propIdEdit_ = nullptr;
    QLineEdit* propTypeEdit_ = nullptr;
    QLineEdit* propXEdit_ = nullptr;
    QLineEdit* propYEdit_ = nullptr;

    // Track edges by "from->to" key
    QHash<QString, EdgeItem*> edgeItems_;
    QHash<QString, NodeItem*> nodeItems_;

    QString currentFilePath_;
};

} // namespace graph_studio

#endif // MAIN_WINDOW_H
