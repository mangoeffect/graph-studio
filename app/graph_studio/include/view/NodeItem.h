#ifndef NODE_ITEM_H
#define NODE_ITEM_H

#include <QGraphicsItem>
#include <QString>

namespace graph_studio {

class NodeItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 1 };

    NodeItem(const QString& nodeId, const QString& nodeType, QGraphicsItem* parent = nullptr);
    ~NodeItem() override;

    QString nodeId() const;
    QString nodeType() const;

    void setSelected(bool selected);
    bool isSelected() const;

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;
    int type() const override;

private:
    QString nodeId_;
    QString nodeType_;
    bool selected_ = false;

    static const qreal NODE_WIDTH;
    static const qreal NODE_HEIGHT;
    static const qreal PORT_RADIUS;
};

} // namespace graph_studio

#endif // NODE_ITEM_H