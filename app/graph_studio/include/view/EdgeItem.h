#ifndef EDGE_ITEM_H
#define EDGE_ITEM_H

#include <QGraphicsItem>

namespace graph_studio {

class NodeItem;

class EdgeItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 2 };

    EdgeItem(NodeItem* source, NodeItem* target, QGraphicsItem* parent = nullptr);
    ~EdgeItem() override;

    NodeItem* sourceNode() const;
    NodeItem* targetNode() const;

    void adjust();

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;
    int type() const override;

private:
    NodeItem* source_;
    NodeItem* target_;

    QPointF sourcePoint_;
    QPointF targetPoint_;

    static const qreal ARROW_SIZE;
};

} // namespace graph_studio

#endif // EDGE_ITEM_H