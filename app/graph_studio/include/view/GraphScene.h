#ifndef GRAPH_SCENE_H
#define GRAPH_SCENE_H

#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QString>

namespace graph_studio {

class NodeItem;
class EdgeItem;

class GraphScene : public QGraphicsScene
{
    Q_OBJECT
public:
    GraphScene(QObject* parent = nullptr);
    ~GraphScene() override;

    NodeItem* findNodeItem(const QString& id) const;

    void setAvailableTaskTypes(const QStringList& types) { availableTaskTypes_ = types; }

signals:
    void edgeCreationRequested(const QString& fromId, const QString& fromPort,
                               const QString& toId, const QString& toPort);
    void nodeMoved(const QString& id, qreal x, qreal y);
    void nodeDoubleClicked(const QString& id);
    void nodeCreateRequested(const QString& taskType, const QPointF& pos);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    bool portDragging_ = false;
    NodeItem* dragSource_ = nullptr;
    QString dragSourcePort_;   // 源节点被点中的输出端口名
    EdgeItem* tempEdge_ = nullptr;
    NodeItem* highlightedTarget_ = nullptr;
    QStringList availableTaskTypes_;
};

} // namespace graph_studio

#endif // GRAPH_SCENE_H
