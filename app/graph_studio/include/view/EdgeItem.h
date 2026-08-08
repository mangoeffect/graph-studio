#ifndef EDGE_ITEM_H
#define EDGE_ITEM_H

#include <QGraphicsItem>
#include <QPointF>
#include <QString>
#include <QRectF>

namespace graph_studio {

class NodeItem;

class EdgeItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 2 };

    // Permanent edge between two nodes (port-aware)
    EdgeItem(NodeItem* source, NodeItem* target,
             const QString& sourcePort = QStringLiteral("out"),
             const QString& targetPort = QStringLiteral("in"),
             QGraphicsItem* parent = nullptr);

    // Temporary edge for drag-preview: source node + free endpoint
    EdgeItem(NodeItem* source, const QPointF& freeEnd,
             const QString& sourcePort = QStringLiteral("out"),
             QGraphicsItem* parent = nullptr);

    ~EdgeItem() override;

    NodeItem* sourceNode() const;
    NodeItem* targetNode() const;
    QString sourcePort() const;
    QString targetPort() const;

    void setTargetNode(NodeItem* target, const QString& targetPort = QStringLiteral("in"));
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

    // 多端口边防重叠：由端口名 hash 决定的确定性 y 向 bow（同一 pair 不同端口
    // 的边曲线错开，标签不互相遮挡）。
    qreal edgeBowOffset() const;
    void paintLabel(QPainter* painter, const QStyleOptionGraphicsItem* option) const;

    NodeItem* source_ = nullptr;
    NodeItem* target_ = nullptr;
    bool isTemporary_ = false;
    QString sourcePort_;
    QString targetPort_;

    QPointF sourcePoint_;
    QPointF targetPoint_;
    QPointF freeEnd_;
    qreal bowOffset_ = 0;

    static const qreal ARROW_SIZE;
};

} // namespace graph_studio

#endif // EDGE_ITEM_H
