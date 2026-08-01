#include "view/GanttChart.h"

#include <QPainter>
#include <QMouseEvent>
#include <QToolTip>
#include <cmath>
#include <algorithm>

using namespace graph_studio;

GanttChart::GanttChart(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(150);
}

void GanttChart::setTasks(double totalMs, const QList<TaskBar>& tasks)
{
    totalMs_ = totalMs;
    tasks_ = tasks;
    int h = TOP_MARGIN + BOTTOM_MARGIN + tasks_.size() * ROW_HEIGHT;
    setMinimumHeight(std::max(150, h));
    update();
}

void GanttChart::setTitle(const QString& title)
{
    title_ = title;
    update();
}

void GanttChart::clear()
{
    totalMs_ = 0;
    tasks_.clear();
    title_.clear();
    hoveredRow_ = -1;
    update();
}

QSize GanttChart::minimumSizeHint() const
{
    return QSize(400, TOP_MARGIN + BOTTOM_MARGIN + std::max(1, (int)tasks_.size()) * ROW_HEIGHT);
}

QSize GanttChart::sizeHint() const
{
    return QSize(600, minimumSizeHint().height());
}

double GanttChart::xFromMs(double ms, int width) const
{
    if (totalMs_ <= 0) return LEFT_MARGIN;
    double avail = width - LEFT_MARGIN - RIGHT_MARGIN - LABEL_WIDTH;
    return LEFT_MARGIN + LABEL_WIDTH + (ms / totalMs_) * avail;
}

double GanttChart::msFromX(int x, int width) const
{
    double avail = width - LEFT_MARGIN - RIGHT_MARGIN - LABEL_WIDTH;
    if (avail <= 0) return 0;
    return ((x - LEFT_MARGIN - LABEL_WIDTH) / avail) * totalMs_;
}

QString GanttChart::formatTime(double ms) const
{
    if (ms < 1.0) return QString::number(ms * 1000, 'f', 0) + "us";
    if (ms < 1000) return QString::number(ms, 'f', 1) + "ms";
    return QString::number(ms / 1000, 'f', 2) + "s";
}

void GanttChart::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    int w = width();
    int h = height();

    // Background
    p.fillRect(rect(), QColor(30, 30, 30));

    if (tasks_.isEmpty() || totalMs_ <= 0) {
        p.setPen(QColor(120, 120, 120));
        p.drawText(rect(), Qt::AlignCenter, "No profile data. Run the graph to see execution timeline.");
        return;
    }

    // Frame title
    if (!title_.isEmpty()) {
        p.setPen(QColor(150, 200, 255));
        QFont titleFont = p.font();
        titleFont.setBold(true);
        titleFont.setPointSize(9);
        p.setFont(titleFont);
        p.drawText(QRectF(LEFT_MARGIN, 2, w - LEFT_MARGIN - RIGHT_MARGIN, 16),
                   Qt::AlignLeft | Qt::AlignVCenter, title_);
    }

    // Time axis
    p.setPen(QColor(140, 140, 140));
    QFont smallFont = p.font();
    smallFont.setPointSize(8);
    p.setFont(smallFont);

    int tickCount = 6;
    for (int i = 0; i <= tickCount; ++i) {
        double ms = totalMs_ * i / tickCount;
        int x = static_cast<int>(xFromMs(ms, w));
        p.drawLine(x, TOP_MARGIN - 4, x, TOP_MARGIN - 1);
        p.drawText(QRectF(x - 40, TOP_MARGIN - 18, 80, 14),
                   Qt::AlignCenter, formatTime(ms));
    }

    // Grid lines
    p.setPen(QPen(QColor(50, 50, 50), 1, Qt::DotLine));
    for (int i = 0; i <= tickCount; ++i) {
        int x = static_cast<int>(xFromMs(totalMs_ * i / tickCount, w));
        p.drawLine(x, TOP_MARGIN, x, h - BOTTOM_MARGIN);
    }

    // Rows
    for (int i = 0; i < tasks_.size(); ++i) {
        const auto& task = tasks_[i];
        int y = TOP_MARGIN + i * ROW_HEIGHT;
        bool hovered = (i == hoveredRow_);

        // Row background (alternating)
        if (i % 2 == 0) {
            p.fillRect(QRect(0, y, w, ROW_HEIGHT), QColor(35, 35, 35));
        }
        if (hovered) {
            p.fillRect(QRect(0, y, w, ROW_HEIGHT), QColor(45, 55, 70));
        }

        // Task label
        p.setPen(QColor(200, 200, 200));
        QFont labelFont = p.font();
        labelFont.setPointSize(8);
        p.setFont(labelFont);
        QString label = task.taskId;
        if (label.length() > 14) label = label.left(12) + "..";
        p.drawText(QRectF(LEFT_MARGIN, y, LABEL_WIDTH - 5, ROW_HEIGHT),
                   Qt::AlignRight | Qt::AlignVCenter, label);

        // Wait bar (ready -> start): gray, semi-transparent
        double waitStart = task.startMs - task.waitMs;
        if (waitStart < 0) waitStart = 0;
        double waitEnd = task.startMs;
        if (waitEnd > waitStart && task.waitMs > 0.01) {
            int wx1 = static_cast<int>(xFromMs(waitStart, w));
            int wx2 = static_cast<int>(xFromMs(waitEnd, w));
            if (wx2 > wx1) {
                QRectF waitRect(wx1, y + (ROW_HEIGHT - BAR_HEIGHT) / 2,
                                wx2 - wx1, BAR_HEIGHT);
                p.fillRect(waitRect, QColor(100, 100, 100, 100));
            }
        }

        // Exec bar (start -> end): colored by status
        int ex1 = static_cast<int>(xFromMs(task.startMs, w));
        int ex2 = static_cast<int>(xFromMs(task.endMs, w));
        if (ex2 <= ex1) ex2 = ex1 + 1;
        QRectF execRect(ex1, y + (ROW_HEIGHT - BAR_HEIGHT) / 2,
                        ex2 - ex1, BAR_HEIGHT);

        QColor execColor;
        if (task.status == 0)       execColor = QColor(76, 175, 80);   // green
        else if (task.status == 1)  execColor = QColor(244, 67, 54);   // red
        else                        execColor = QColor(255, 193, 7);   // yellow

        if (hovered) execColor = execColor.lighter(130);

        p.fillRect(execRect, execColor);
        p.setPen(QPen(execColor.darker(150), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(execRect, 2, 2);

        // Exec duration text on bar (if wide enough)
        if (ex2 - ex1 > 40) {
            p.setPen(QColor(255, 255, 255));
            QFont barFont = p.font();
            barFont.setPointSize(7);
            p.setFont(barFont);
            p.drawText(execRect, Qt::AlignCenter,
                       QString::number(task.execMs, 'f', 1) + "ms");
        }
    }

    // Legend
    p.setPen(Qt::NoPen);
    int legY = h - BOTTOM_MARGIN + 2;
    p.setBrush(QColor(76, 175, 80));
    p.drawRect(QRect(LEFT_MARGIN, legY, 10, 8));
    p.setPen(QColor(180, 180, 180));
    p.drawText(QRect(LEFT_MARGIN + 13, legY - 2, 60, 12), Qt::AlignLeft, "completed");

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(244, 67, 54));
    p.drawRect(QRect(LEFT_MARGIN + 80, legY, 10, 8));
    p.setPen(QColor(180, 180, 180));
    p.drawText(QRect(LEFT_MARGIN + 93, legY - 2, 50, 12), Qt::AlignLeft, "failed");

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 193, 7));
    p.drawRect(QRect(LEFT_MARGIN + 145, legY, 10, 8));
    p.setPen(QColor(180, 180, 180));
    p.drawText(QRect(LEFT_MARGIN + 158, legY - 2, 50, 12), Qt::AlignLeft, "skipped");

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(100, 100, 100, 100));
    p.drawRect(QRect(LEFT_MARGIN + 210, legY, 10, 8));
    p.setPen(QColor(180, 180, 180));
    p.drawText(QRect(LEFT_MARGIN + 223, legY - 2, 50, 12), Qt::AlignLeft, "waiting");
}

void GanttChart::mouseMoveEvent(QMouseEvent* event)
{
    int y = event->pos().y();
    int row = (y - TOP_MARGIN) / ROW_HEIGHT;

    if (row >= 0 && row < tasks_.size()) {
        if (row != hoveredRow_) {
            hoveredRow_ = row;
            update();
        }
        const auto& task = tasks_[row];
        QString tip = QString("%1 (%2)\nWait: %3 ms  Exec: %4 ms  Total: %5 ms")
                          .arg(task.taskId, task.taskType)
                          .arg(task.waitMs, 0, 'f', 2)
                          .arg(task.execMs, 0, 'f', 2)
                          .arg(task.waitMs + task.execMs, 0, 'f', 2);
        QToolTip::showText(event->globalPos(), tip, this);
    } else {
        if (hoveredRow_ != -1) {
            hoveredRow_ = -1;
            update();
        }
        QToolTip::hideText();
    }
}

void GanttChart::leaveEvent(QEvent*)
{
    hoveredRow_ = -1;
    update();
}
