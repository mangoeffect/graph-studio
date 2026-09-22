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
#include <QTemporaryDir>

#include <memory>

#include "view/GraphView.h"
#include "viewmodel/GraphViewModel.h"
#include "command/CommandStack.h"

namespace graph_studio {

class GraphScene;
class GraphViewModel;
class NodeItem;
class EdgeItem;
class ProfilePanel;
class ImageViewer;

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
    // 画布内节点端口锚点（供 WASM E2E 测试桥定坐标）：viewport 像素坐标
    // + 节点尺寸 + 节点中心（cx/cy）；x<0 表示节点/端口不存在。
    struct NodePortAnchor
    {
        double x = -1;
        double y = -1;
        double w = 0;
        double h = 0;
        double cx = -1;
        double cy = -1;
    };

    MainWindow(GraphViewModel& vm, QWidget* parent = nullptr);
    ~MainWindow() override;

    // ---- 外部 UI 自动化（E2E）桥接：仅供 test_hooks / 测试调用 ----
    // 按 action 文本前缀触发（如 "Undo"/"Redo"/"Delete"），未命中返回 false。
    bool triggerAction(const QString& textStartsWith);
    NodePortAnchor nodePortAnchor(const QString& nodeId, const QString& port) const;
    // 场景侧全部节点 id（对照 VM 侧，E2E 调试用）
    QStringList sceneNodeIds() const { return nodeItems_.keys(); }
    // CLI 启动参数通道（--open <file> [--run]）：启动即打开图、可选立即执行。
    // 失败只走 loadFromFile 的日志路径（无弹窗），返回是否打开成功。
    bool OpenGraphAtStartup(const QString& path, bool run);
    // 打开 .tgp 工程包（wasm 交换通道 / 嗅探后的 File→Open 共用）：
    // 解包到会话临时目录并加载内部图；manifest.missing 非空时 WARN 进日志
    // 面板。displayName 非空时用于日志/标题（wasm 交换通道的包字节在临时
    // 文件里，展示名须由通道传入）。返回是否成功。
    bool OpenProjectFile(const QString& tgpPath, bool runAfterLoad = false,
                         const QString& displayName = QString());
    // 从 UI 之外（wasm 交换通道等）向日志面板追加一条：level 取
    // task_graph::LogLevel 的 int 值，与 logMessage 信号同语义（面板 + [gs] 镜像）。
    void PostLog(int level, const QString& msg);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
#ifdef __EMSCRIPTEN__
    // 任务库手写拖拽状态（QDrag 发起需 asyncify，本构建不带；且 wasm 平台
    // 无效的 grabMouse 会让"跨控件 move/release"回不到列表控件，故用应用级
    // 事件过滤器全程观察 press/move/release——见 eventFilter）。
    QString tlwPressItem_;
    QPoint tlwPressGlobal_;
    bool tlwManualDragging_ = false;
#endif

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
    // backend 类 enum 参数变化后的联动：后端专属参数显隐 + reset_on_link
    // 参数（隐藏的 model_path）还原默认值
    void ApplyParamLinkEffects(const QString& changedKey);
    void OnBrowseFile(const QString& key, QLineEdit* le, const QString& filter);

    // Actions
    void ActionNew();
    void ActionOpen();
    void ActionSave();
    void ActionSaveAs();
    void ActionExportProject();
    void ActionAutoLayout();
    void ActionZoomIn();
    void ActionZoomOut();
    void ActionFitToView();
    void ActionUndo();
    void ActionRedo();
    void ActionRun();
    void ActionStop();
    void ActionRunN();
    void ActionRunLoop();
    void ActionPauseResume();
    void UpdateUndoRedoActions();
    void UpdateRunActions();

    // 加载一个图文件（File→Open 与拖放共用）：成功返回 true 并更新当前文件与标题。
    bool OpenGraphFile(const QString& path);
    void UpdateWindowTitle();

    // 图像结果面板：根据当前下拉选中显示对应 QImage；填充下拉列表
    void ShowResultImage(const QString& key);
    // 结果图面板的统一入口（img.isNull() 即清空）。查看器为纯 QPainter
    // 绘制（桌面/WASM 同一 ImageViewer）。
    void ShowViewerImage(const QImage& image);
    void RebuildResultSelector(const QStringList& keys);

    GraphViewModel& vm_;
    QSplitter* mainSplitter_ = nullptr;
    QSplitter* topSplitter_ = nullptr;
    QSplitter* bottomSplitter_ = nullptr;
    QToolBar* toolbar_ = nullptr;
    GraphView* graphicsView_ = nullptr;
    QGroupBox* canvasHost_ = nullptr;  // 画布 AX 锚点容器（QGraphicsView 本体不进 AX 树）
    GraphScene* scene_ = nullptr;
    QStatusBar* statusBar_ = nullptr;

    TaskListWidget* taskList_ = nullptr;
    ImageViewer* imageViewer_ = nullptr;            // 结果图查看器（全平台）
    QLabel* pixelInfoLabel_ = nullptr;
    QComboBox* resultSelector_ = nullptr;
    QFormLayout* nodePropertyLayout_ = nullptr;
    QGroupBox* nodePropertyGroup_ = nullptr;
    // 动态参数表单：选中节点时按 paramSpecs 重建控件
    QFormLayout* paramsLayout_ = nullptr;
    QGroupBox* paramsGroup_ = nullptr;
    QHash<QString, QWidget*> paramWidgets_;  // key -> 当前生成的控件
    QVariantList paramSpecsCache_;  // 当前面板对应的 paramSpecs（联动显隐用）
    // 标记参数变更由当前正在编辑的控件触发（OnParamWidgetChanged 路径），
    // 避免其回响 nodeParamsChanged 时又 RebuildParamWidgets 删除自己（use-after-free）。
    bool selfParamEdit_ = false;
    QPlainTextEdit* logWidget_ = nullptr;
    QPlainTextEdit* outputWidget_ = nullptr;
    QTabWidget* bottomTabs_ = nullptr;
    ProfilePanel* profilePanel_ = nullptr;
    QLabel* zoomLabel_ = nullptr;
    QLabel* countsLabel_ = nullptr;

    // Property panel widgets
    QLineEdit* propIdEdit_ = nullptr;
    QLineEdit* propTypeEdit_ = nullptr;
    QLineEdit* propXEdit_ = nullptr;
    QLineEdit* propYEdit_ = nullptr;
    // 节点使用说明链接（官网节点手册 /blog/<task_type>/，随选中节点切换 href）
    QLabel* docsLinkLabel_ = nullptr;

    // Track edges by "from->to" key
    QHash<QString, EdgeItem*> edgeItems_;
    QHash<QString, NodeItem*> nodeItems_;

    QString currentFilePath_;

    // ---- .tgp 工程包会话态：打开工程时解包目录由会话持有（New/Open 普通
    // 图/再开工程时释放即自动清理）；projectMode_ 下 Save/Save As 转为
    // 重新导出新包（v1 工程只读，不做就地回写）。
    bool projectMode_ = false;
    std::unique_ptr<QTemporaryDir> projectDir_;
    QString projectGraphPath_;  // 解包出的图 JSON 路径（导出时的打包源）

    CommandStack commandStack_;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* runAction_ = nullptr;
    QAction* stopAction_ = nullptr;
    QAction* pauseAction_ = nullptr;    // 运行中暂停/继续（同一项切换文案）
    QAction* runNAction_ = nullptr;     // 连续 N 次（N 由对话框输入）
    QAction* runLoopAction_ = nullptr;  // 循环运行（直到 Stop）
};

} // namespace graph_studio

#endif // MAIN_WINDOW_H
