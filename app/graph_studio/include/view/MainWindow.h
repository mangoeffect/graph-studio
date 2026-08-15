#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include <QToolBar>
#include <QStatusBar>
#include <QTreeWidget>
#include <QPlainTextEdit>
#include <QLabel>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QHash>
#include <QMimeData>
#include <QComboBox>
#include <QTabWidget>

#include "view/GraphView.h"
#include "viewmodel/GraphViewModel.h"
#include "command/CommandStack.h"

namespace graph_studio {

class GraphScene;
class GraphViewModel;
class NodeItem;
class EdgeItem;
class ProfilePanel;
class GpuImageViewer;

// QTreeWidget subclass that emits plain-text mime data on drag, so the canvas
// GraphView (which checks hasText()) can accept the drop. Categories are
// top-level (collapsed-by-default) parents; task types are their children.
class TaskListWidget : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;

protected:
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override
    {
        // Only emit text for the first draggable item (single selection drag);
        // category parents are not drag-enabled and thus skipped.
        for (auto* item : items) {
            if (item->flags() & Qt::ItemIsDragEnabled) {
                QMimeData* mimeData = new QMimeData;
                mimeData->setText(item->text(0));
                return mimeData;
            }
        }
        return nullptr;
    }
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(GraphViewModel& vm, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onTaskAdded(const NodeData& node);
    void onTaskRemoved(const QString& taskId);
    void onEdgeAdded(const EdgeData& edge);
    void onEdgeRemoved(const EdgeData& edge);
    void onNodeMovedVm(const QString& id, qreal x, qreal y);
    void onGraphReset();
    void onLogMessage(int level, const QString& msg);
    void onSelectionChangedVm(const QString& nodeId);
    void onSceneSelectionChanged();
    void onEdgeCreationRequested(const QString& fromId, const QString& fromPort,
                                 const QString& toId, const QString& toPort);
    void onNodeMovedScene(const QString& id, qreal x, qreal y);
    void onNodeDoubleClicked(const QString& id);

    // 执行相关槽（由 ViewModel 排队信号驱动，均在 UI 线程执行）
    void onNodeStatusChanged(const QString& id, int phase, double durationMs);
    void onExecutionStarted();
    void onExecutionFinished();
    void onExecutingChanged();

    // 图像结果面板：执行后采集到的各节点图像结果
    void onImageResultsReady(const QStringList& keys);
    void onResultSelectorChanged(int index);

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
    QWidget* CreateBottomTabs();
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
    void RebuildParamWidgets(const QString& nodeId);
    void OnParamWidgetChanged(const QString& key);
    void OnBrowseFile(const QString& key, QLineEdit* le, const QString& filter);

    // Actions
    void ActionNew();
    void ActionOpen();
    void ActionSave();
    void ActionSaveAs();
    void ActionAutoLayout();
    void ActionZoomIn();
    void ActionZoomOut();
    void ActionFitToView();
    void ActionUndo();
    void ActionRedo();
    void ActionRun();
    void ActionStop();
    void UpdateUndoRedoActions();
    void UpdateRunActions();

    // 加载一个图文件（File→Open 与拖放共用）：成功返回 true 并更新当前文件与标题。
    bool OpenGraphFile(const QString& path);
    void UpdateWindowTitle();

    // 图像结果面板：根据当前下拉选中显示对应 QImage；填充下拉列表
    void ShowResultImage(const QString& key);
    // 结果图面板的统一入口（img.isNull() 即清空）。桌面走 GpuImageViewer，
    // WASM 退化为 QLabel（见 .cpp 的 __EMSCRIPTEN__ 分支）。
    void ShowViewerImage(const QImage& image);
    void RebuildResultSelector(const QStringList& keys);

    GraphViewModel& vm_;
    QSplitter* mainSplitter_ = nullptr;
    QSplitter* topSplitter_ = nullptr;
    QSplitter* bottomSplitter_ = nullptr;
    QToolBar* toolbar_ = nullptr;
    GraphView* graphicsView_ = nullptr;
    GraphScene* scene_ = nullptr;
    QStatusBar* statusBar_ = nullptr;

    TaskListWidget* taskList_ = nullptr;
    GpuImageViewer* imageViewer_ = nullptr;          // 桌面端（WASM 恒为 null）
    QLabel* imageViewerFallback_ = nullptr;          // WASM 的 QLabel 退化视图
    QLabel* pixelInfoLabel_ = nullptr;
    QComboBox* resultSelector_ = nullptr;
    QFormLayout* nodePropertyLayout_ = nullptr;
    QGroupBox* nodePropertyGroup_ = nullptr;
    // 动态参数表单：选中节点时按 paramSpecs 重建控件
    QFormLayout* paramsLayout_ = nullptr;
    QGroupBox* paramsGroup_ = nullptr;
    QHash<QString, QWidget*> paramWidgets_;  // key -> 当前生成的控件
    // 标记参数变更由当前正在编辑的控件触发（OnParamWidgetChanged 路径），
    // 避免其回响 nodeParamsChanged 时又 RebuildParamWidgets 删除自己（use-after-free）。
    bool selfParamEdit_ = false;
    QPlainTextEdit* logWidget_ = nullptr;
    QPlainTextEdit* outputWidget_ = nullptr;
    QTabWidget* bottomTabs_ = nullptr;
    ProfilePanel* profilePanel_ = nullptr;
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

    CommandStack commandStack_;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* runAction_ = nullptr;
    QAction* stopAction_ = nullptr;
};

} // namespace graph_studio

#endif // MAIN_WINDOW_H
