#ifndef EDGE_ITEM_H
#define EDGE_ITEM_H

#include <QGraphicsItem>
#include <QPointF>

namespace graph_studio {

class NodeItem;

class EdgeItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 2 };

    // Permanent edge between two nodes
    EdgeItem(NodeItem* source, NodeItem* target, QGraphicsItem* parent = nullptr);

    // Temporary edge for drag-preview: source node + free endpoint
    EdgeItem(NodeItem* source, const QPointF& freeEnd, QGraphicsItem* parent = nullptr);

    ~EdgeItem() override;

    NodeItem* sourceNode() const;
    NodeItem* targetNode() const;

    void setTargetNode(NodeItem* target);
    void setFreeEnd(const QPointF& pos);
    void adjust();

    QString fromId() const;
    QString toId() const;

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;
    int type() const override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    void registerWithNodes();
    void unregisterFromNodes();

    NodeItem* source_ = nullptr;
    NodeItem* target_ = nullptr;
    bool isTemporary_ = false;

    QPointF sourcePoint_;
    QPointF targetPoint_;
    QPointF freeEnd_;

    static const qreal ARROW_SIZE;
};

} // namespace graph_studio

#endif // EDGE_ITEM_H
