#ifndef GRAPH_VIEW_H
#define GRAPH_VIEW_H

#include <QGraphicsView>
#include <QString>
#include <QPointF>

namespace graph_studio {

class GraphView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit GraphView(QGraphicsScene* scene, QWidget* parent = nullptr);
    ~GraphView() override;

signals:
    void taskDropped(const QString& taskType, const QPointF& scenePos);
    void zoomChanged(qreal factor);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    bool panning_ = false;
    QPointF panStart_;
    QPointF panOrigin_;
};

} // namespace graph_studio

#endif // GRAPH_VIEW_H
