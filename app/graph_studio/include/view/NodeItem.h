#ifndef NODE_ITEM_H
#define NODE_ITEM_H

#include <QGraphicsItem>
#include <QString>
#include <QSet>

namespace graph_studio {

class EdgeItem;

class NodeItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 1 };
    enum class Port { None, Input, Output };
    enum class RunStatus { None, Running, Completed, Failed };

    NodeItem(const QString& nodeId, const QString& nodeType, QGraphicsItem* parent = nullptr);
    ~NodeItem() override;

    QString nodeId() const;
    QString nodeType() const;
    void setNodeType(const QString& type);

    // 执行状态：驱动节点边框/发光配色（运行中/成功/失败）
    void setRunStatus(RunStatus status);
    RunStatus runStatus() const;

    // Port geometry in scene coordinates
    QPointF inputPortPos() const;
    QPointF outputPortPos() const;

    // Hit-test a scene point against ports
    Port hitPort(const QPointF& scenePos) const;

    // Highlight target input port during edge-drag (drop target feedback)
    void setDropHighlighted(bool on);

    // Edge registration for movement tracking
    void registerEdge(EdgeItem* edge);
    void unregisterEdge(EdgeItem* edge);

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;
    int type() const override;

    static qreal nodeWidth() { return NODE_WIDTH; }
    static qreal nodeHeight() { return NODE_HEIGHT; }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    void notifyEdges();

    QString nodeId_;
    QString nodeType_;
    QSet<EdgeItem*> edges_;
    RunStatus runStatus_ = RunStatus::None;
    Port hoverPort_ = Port::None;
    bool dropHighlighted_ = false;

    static const qreal NODE_WIDTH;
    static const qreal NODE_HEIGHT;
    static const qreal PORT_RADIUS;
    static const qreal PORT_HIT_RADIUS;
};

} // namespace graph_studio

#endif // NODE_ITEM_H
