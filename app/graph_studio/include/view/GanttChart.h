#ifndef GANTT_CHART_H
#define GANTT_CHART_H

#include <QWidget>
#include <QString>
#include <QList>
#include <QRectF>

namespace graph_studio {

class GanttChart : public QWidget
{
    Q_OBJECT
public:
    struct TaskBar {
        QString taskId;
        QString taskType;
        double startMs;
        double endMs;
        double waitMs;
        double execMs;
        int status;  // 0=completed 1=failed 2=skipped
    };

    explicit GanttChart(QWidget* parent = nullptr);

    void setTasks(double totalMs, const QList<TaskBar>& tasks);
    void setTitle(const QString& title);
    void clear();

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    double totalMs_ = 0;
    QList<TaskBar> tasks_;
    QString title_;

    static constexpr int LABEL_WIDTH = 100;
    static constexpr int ROW_HEIGHT = 22;
    static constexpr int TOP_MARGIN = 30;
    static constexpr int BOTTOM_MARGIN = 10;
    static constexpr int LEFT_MARGIN = 5;
    static constexpr int RIGHT_MARGIN = 15;
    static constexpr int BAR_HEIGHT = 14;

    int hoveredRow_ = -1;

    double xFromMs(double ms, int width) const;
    double msFromX(int x, int width) const;
    QString formatTime(double ms) const;
};

} // namespace graph_studio

#endif // GANTT_CHART_H
