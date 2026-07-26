#ifndef GRAPH_SCENE_H
#define GRAPH_SCENE_H

#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>

namespace graph_studio {

class GraphScene : public QGraphicsScene
{
    Q_OBJECT
public:
    GraphScene(QObject* parent = nullptr);
    ~GraphScene() override;

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    bool isDragging_ = false;
    QPointF dragStart_;
};

} // namespace graph_studio

#endif // GRAPH_SCENE_H