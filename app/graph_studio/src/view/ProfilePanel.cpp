#include "view/ProfilePanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QScrollBar>

using namespace graph_studio;

ProfilePanel::ProfilePanel(GraphViewModel& vm, QWidget* parent)
    : QWidget(parent), vm_(vm)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // ── Top bar: frame selector + clear ──
    auto* topBar = new QHBoxLayout();
    auto* frameLabel = new QLabel("Frame:");
    frameLabel->setStyleSheet("color: #d4d4d4; font-weight: bold;");
    topBar->addWidget(frameLabel);

    frameSelector_ = new QComboBox();
    frameSelector_->setMinimumWidth(180);
    frameSelector_->setEnabled(false);
    topBar->addWidget(frameSelector_);

    btnClear_ = new QPushButton("Clear History");
    btnClear_->setEnabled(false);
    topBar->addWidget(btnClear_);
    topBar->addStretch();
    layout->addLayout(topBar);

    // ── Summary ──
    summaryLabel_ = new QLabel("No profile data. Run the graph to see execution analysis.");
    QFont sumFont = summaryLabel_->font();
    sumFont.setBold(true);
    sumFont.setPointSize(11);
    summaryLabel_->setFont(sumFont);
    summaryLabel_->setStyleSheet("padding: 4px; color: #d4d4d4;");
    layout->addWidget(summaryLabel_);

    // ── Gantt Chart (scrollable) ──
    auto* ganttScroll = new QScrollArea();
    ganttScroll->setWidgetResizable(true);
    ganttChart_ = new GanttChart();
    ganttScroll->setWidget(ganttChart_);
    ganttScroll->setMinimumHeight(180);
    layout->addWidget(ganttScroll, 2);

    // ── Stats Table ──
    statsTable_ = new QTableWidget();
    statsTable_->setColumnCount(7);
    statsTable_->setHorizontalHeaderLabels(
        {"Task ID", "Type", "Wait (ms)", "Exec (ms)", "Total (ms)", "Status", "Frames"});
    statsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    statsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    statsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    statsTable_->setAlternatingRowColors(true);
    statsTable_->setMaximumHeight(160);
    layout->addWidget(statsTable_);

    // ── Event Stream ──
    auto* eventHeader = new QLabel("Execution Event Stream");
    QFont ehFont = eventHeader->font();
    ehFont.setBold(true);
    ehFont.setPointSize(10);
    eventHeader->setFont(ehFont);
    eventHeader->setStyleSheet("padding: 2px;");
    layout->addWidget(eventHeader);

    eventStream_ = new QPlainTextEdit();
    eventStream_->setReadOnly(true);
    eventStream_->setMaximumBlockCount(5000);
    eventStream_->setMaximumHeight(120);
    layout->addWidget(eventStream_);

    // ── Export buttons ──
    auto* btnBar = new QHBoxLayout();
    btnExportTrace_ = new QPushButton("Export Chrome Trace JSON");
    btnExportReport_ = new QPushButton("Export JSON Report");
    btnExportTrace_->setEnabled(false);
    btnExportReport_->setEnabled(false);
    btnBar->addWidget(btnExportTrace_);
    btnBar->addWidget(btnExportReport_);
    btnBar->addStretch();
    layout->addLayout(btnBar);

    connect(btnExportTrace_, &QPushButton::clicked, this, &ProfilePanel::onExportTrace);
    connect(btnExportReport_, &QPushButton::clicked, this, &ProfilePanel::onExportReport);
    connect(btnClear_, &QPushButton::clicked, this, &ProfilePanel::onClearHistory);
    connect(frameSelector_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ProfilePanel::onFrameChanged);
}

void ProfilePanel::onProfileDataReady(int frameIndex)
{
    rebuildFrameSelector(frameIndex);
    btnExportTrace_->setEnabled(true);
    btnExportReport_->setEnabled(true);
    btnClear_->setEnabled(true);
}

void ProfilePanel::rebuildFrameSelector(int currentFrameIndex)
{
    frameSelector_->blockSignals(true);
    frameSelector_->clear();

    int count = vm_.profileFrameCount();
    for (int i = 0; i < count; ++i) {
        const auto* f = vm_.profileFrame(i);
        if (!f) continue;
        frameSelector_->addItem(QString("Frame %1  (%2 ms, %3/%4 ok)")
                                    .arg(i)
                                    .arg(f->dag.totalMs, 0, 'f', 1)
                                    .arg(f->dag.completedTasks)
                                    .arg(f->dag.totalTasks), i);
    }
    if (count >= 2) {
        frameSelector_->insertItem(0, QString("Average (%1 frames)").arg(count), -1);
    }

    frameSelector_->setEnabled(count > 0);

    // Select the new frame by default, or "Average" if available
    if (count >= 2) {
        // Select average view for new frames (Tracy-like default)
        // But if this is the first frame, select it
        if (currentFrameIndex == 0 && count == 1) {
            frameSelector_->setCurrentIndex(0);
            selectedView_ = 0;
        } else {
            frameSelector_->setCurrentIndex(0);  // "Average"
            selectedView_ = -1;
        }
    } else if (count == 1) {
        frameSelector_->setCurrentIndex(0);
        selectedView_ = 0;
    }

    frameSelector_->blockSignals(false);
    refreshView();
}

void ProfilePanel::onFrameChanged(int comboIndex)
{
    if (comboIndex < 0) return;
    QVariant data = frameSelector_->itemData(comboIndex);
    selectedView_ = data.toInt();
    refreshView();
}

void ProfilePanel::refreshView()
{
    if (selectedView_ == -2) return;

    int frameCount = vm_.profileFrameCount();
    if (frameCount == 0) return;

    GraphViewModel::ProfileFrame frame;

    if (selectedView_ == -1) {
        // Average
        frame = vm_.profileAverage();
    } else {
        const auto* fp = vm_.profileFrame(selectedView_);
        if (!fp) return;
        frame = *fp;
    }

    int displayFrame = (selectedView_ == -1) ? -1 : selectedView_;
    populateSummary(frame.dag, frameCount, displayFrame);

    QString ganttTitle;
    if (selectedView_ == -1) {
        ganttTitle = QString("Average of %1 frames").arg(frameCount);
    } else {
        ganttTitle = QString("Frame %1").arg(selectedView_);
    }
    populateGantt(frame.dag.totalMs, frame.tasks, ganttTitle);
    populateTable(frame.tasks);
    populateEventStream(frame.tasks, frame.dag);
}

void ProfilePanel::populateSummary(const GraphViewModel::ProfileDagInfo& dag,
                                    int frameCount, int selectedFrame)
{
    double parallelEff = 0;
    if (dag.totalMs > 0 && dag.criticalPathMs > 0) {
        parallelEff = (dag.criticalPathMs / dag.totalMs) * 100;
    }

    QString frameDesc;
    if (selectedFrame == -1) {
        frameDesc = QString("Average of %1 frames").arg(frameCount);
    } else {
        frameDesc = QString("Frame %1 of %2").arg(selectedFrame).arg(frameCount - 1);
    }

    summaryLabel_->setText(QString(
        "%1 | %2 ms total | %3 tasks (%4 ok, %5 failed, %6 skipped) | "
        "Critical Path: %7 ms | Parallel Efficiency: %8%")
        .arg(frameDesc)
        .arg(dag.totalMs, 0, 'f', 1)
        .arg(dag.totalTasks)
        .arg(dag.completedTasks)
        .arg(dag.failedTasks)
        .arg(dag.skippedTasks)
        .arg(dag.criticalPathMs, 0, 'f', 1)
        .arg(parallelEff, 0, 'f', 0));
}

void ProfilePanel::populateGantt(double totalMs,
                                  const QList<GraphViewModel::ProfileTaskInfo>& tasks,
                                  const QString& title)
{
    QList<GanttChart::TaskBar> bars;
    for (const auto& t : tasks) {
        bars.append({t.taskId, t.taskType, t.startMs, t.endMs,
                      t.waitMs, t.execMs, t.status});
    }
    ganttChart_->setTitle(title);
    ganttChart_->setTasks(totalMs, bars);
}

void ProfilePanel::populateTable(const QList<GraphViewModel::ProfileTaskInfo>& tasks)
{
    int frameCount = vm_.profileFrameCount();
    statsTable_->setRowCount(tasks.size());
    for (int i = 0; i < tasks.size(); ++i) {
        const auto& t = tasks[i];
        statsTable_->setItem(i, 0, new QTableWidgetItem(t.taskId));
        statsTable_->setItem(i, 1, new QTableWidgetItem(t.taskType));
        statsTable_->setItem(i, 2, new QTableWidgetItem(QString::number(t.waitMs, 'f', 2)));
        statsTable_->setItem(i, 3, new QTableWidgetItem(QString::number(t.execMs, 'f', 2)));
        statsTable_->setItem(i, 4, new QTableWidgetItem(QString::number(t.totalMs, 'f', 2)));

        QString statusStr;
        QColor statusColor;
        if (t.status == 0) { statusStr = "Completed"; statusColor = QColor(76, 175, 80); }
        else if (t.status == 1) { statusStr = "Failed"; statusColor = QColor(244, 67, 54); }
        else { statusStr = "Skipped"; statusColor = QColor(255, 193, 7); }

        auto* statusItem = new QTableWidgetItem(statusStr);
        statusItem->setForeground(statusColor);
        statsTable_->setItem(i, 5, statusItem);

        statsTable_->setItem(i, 6, new QTableWidgetItem(QString::number(frameCount)));
    }
    statsTable_->sortByColumn(4, Qt::DescendingOrder);
}

void ProfilePanel::populateEventStream(const QList<GraphViewModel::ProfileTaskInfo>& tasks,
                                        const GraphViewModel::ProfileDagInfo& dag)
{
    eventStream_->clear();

    eventStream_->appendHtml(QString("<span style='color:#4fc3f7'>[DAG] Execution started: %1 tasks</span>")
                                 .arg(dag.totalTasks));

    auto sorted = tasks;
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.startMs < b.startMs; });

    for (const auto& t : sorted) {
        QString color;
        QString phase;
        if (t.status == 0) { color = "#81c784"; phase = "COMPLETED"; }
        else if (t.status == 1) { color = "#e57373"; phase = "FAILED"; }
        else { color = "#ffb74d"; phase = "SKIPPED"; }

        eventStream_->appendHtml(QString(
            "<span style='color:%1'>[%2 ms] %3 (%4) -> %5 | wait=%6ms exec=%7ms total=%8ms</span>")
            .arg(color)
            .arg(t.startMs, 0, 'f', 1)
            .arg(t.taskId, t.taskType, phase)
            .arg(t.waitMs, 0, 'f', 2)
            .arg(t.execMs, 0, 'f', 2)
            .arg(t.totalMs, 0, 'f', 2));
    }

    eventStream_->appendHtml(QString(
        "<span style='color:#4fc3f7'>[DAG] Execution finished: %1 ms total, %2 ok, %3 failed</span>")
        .arg(dag.totalMs, 0, 'f', 1)
        .arg(dag.completedTasks)
        .arg(dag.failedTasks));
}

void ProfilePanel::onExportTrace()
{
    // Export selected frame's trace, or last frame if average view
    int frameIdx = (selectedView_ >= 0) ? selectedView_ : vm_.profileFrameCount() - 1;
    const auto* frame = vm_.profileFrame(frameIdx);
    if (!frame) return;

    QString json = frame->traceJson;
    if (json.isEmpty()) return;

#ifdef __EMSCRIPTEN__
    QFileDialog::saveFileContent(json.toUtf8(), "profile_trace.json");
#else
    QString path = QFileDialog::getSaveFileName(this, "Export Chrome Trace",
        "profile_trace.json", "JSON Files (*.json)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".json")) path += ".json";
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(json.toUtf8());
#endif
}

void ProfilePanel::onExportReport()
{
    int frameIdx = (selectedView_ >= 0) ? selectedView_ : vm_.profileFrameCount() - 1;
    const auto* frame = vm_.profileFrame(frameIdx);
    if (!frame) return;

    QString json = frame->reportJson;
    if (json.isEmpty()) return;

#ifdef __EMSCRIPTEN__
    QFileDialog::saveFileContent(json.toUtf8(), "profile_report.json");
#else
    QString path = QFileDialog::getSaveFileName(this, "Export JSON Report",
        "profile_report.json", "JSON Files (*.json)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".json")) path += ".json";
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(json.toUtf8());
#endif
}

void ProfilePanel::onClearHistory()
{
    vm_.clearProfileHistory();
    frameSelector_->blockSignals(true);
    frameSelector_->clear();
    frameSelector_->setEnabled(false);
    frameSelector_->blockSignals(false);
    btnClear_->setEnabled(false);
    btnExportTrace_->setEnabled(false);
    btnExportReport_->setEnabled(false);
    selectedView_ = -2;
    summaryLabel_->setText("Profile history cleared. Run the graph to collect new data.");
    ganttChart_->clear();
    statsTable_->setRowCount(0);
    eventStream_->clear();
}
