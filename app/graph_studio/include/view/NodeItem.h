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

    NodeItem(const QString& nodeId, const QString& nodeType, QGraphicsItem* parent = nullptr);
    ~NodeItem() override;

    QString nodeId() const;
    QString nodeType() const;
    void setNodeType(const QString& type);

    // Port geometry in scene coordinates
    QPointF inputPortPos() const;
    QPointF outputPortPos() const;

    // Hit-test a scene point against ports
    Port hitPort(const QPointF& scenePos) const;

    // Edge registration for movement tracking
    void registerEdge(EdgeItem* edge);
    void unregisterEdge(EdgeItem* edge);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;
    int type() const override;

    static qreal nodeWidth() { return NODE_WIDTH; }
    static qreal nodeHeight() { return NODE_HEIGHT; }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    void notifyEdges();

    QString nodeId_;
    QString nodeType_;
    QSet<EdgeItem*> edges_;

    static const qreal NODE_WIDTH;
    static const qreal NODE_HEIGHT;
    static const qreal PORT_RADIUS;
};

} // namespace graph_studio

#endif // NODE_ITEM_H
