#ifndef PROFILE_PANEL_H
#define PROFILE_PANEL_H

#include <QWidget>
#include <QLabel>
#include <QTableWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QComboBox>

#include "view/GanttChart.h"
#include "viewmodel/GraphViewModel.h"

namespace graph_studio {

class ProfilePanel : public QWidget
{
    Q_OBJECT
public:
    explicit ProfilePanel(GraphViewModel& vm, QWidget* parent = nullptr);

public slots:
    void onProfileDataReady(int frameIndex);

private slots:
    void onFrameChanged(int comboIndex);
    void onExportTrace();
    void onExportReport();
    void onClearHistory();

private:
    GraphViewModel& vm_;

    // Frame selector
    QComboBox* frameSelector_ = nullptr;
    QPushButton* btnClear_ = nullptr;

    QLabel* summaryLabel_ = nullptr;
    GanttChart* ganttChart_ = nullptr;
    QTableWidget* statsTable_ = nullptr;
    QPlainTextEdit* eventStream_ = nullptr;
    QPushButton* btnExportTrace_ = nullptr;
    QPushButton* btnExportReport_ = nullptr;

    // Currently selected view: -1 = average, >=0 = specific frame
    int selectedView_ = -2;  // -2 = nothing selected yet

    void refreshView();
    void populateSummary(const GraphViewModel::ProfileDagInfo& dag, int frameCount, int selectedFrame);
    void populateGantt(double totalMs, const QList<GraphViewModel::ProfileTaskInfo>& tasks, const QString& title);
    void populateTable(const QList<GraphViewModel::ProfileTaskInfo>& tasks);
    void populateEventStream(const QList<GraphViewModel::ProfileTaskInfo>& tasks,
                             const GraphViewModel::ProfileDagInfo& dag);
    void rebuildFrameSelector(int currentFrameIndex);
};

} // namespace graph_studio

#endif // PROFILE_PANEL_H
