#include "view/EdgeItem.h"
#include "view/NodeItem.h"

#include <QPainter>

using namespace graph_studio;

const qreal EdgeItem::ARROW_SIZE = 10;

EdgeItem::EdgeItem(NodeItem* source, NodeItem* target, QGraphicsItem* parent)
    : QGraphicsItem(parent), source_(source), target_(target)
{
    setZValue(-1);
    adjust();
}

EdgeItem::~EdgeItem() = default;

NodeItem* EdgeItem::sourceNode() const
{
    return source_;
}

NodeItem* EdgeItem::targetNode() const
{
    return target_;
}

void EdgeItem::adjust()
{
    if (!source_ || !target_)
        return;

    QRectF sourceRect = source_->boundingRect();
    QRectF targetRect = target_->boundingRect();

    sourcePoint_ = source_->mapToScene(QPointF(sourceRect.right(), sourceRect.center().y()));
    targetPoint_ = target_->mapToScene(QPointF(targetRect.left(), targetRect.center().y()));

    prepareGeometryChange();
}

QRectF EdgeItem::boundingRect() const
{
    if (!source_ || !target_)
        return QRectF();

    qreal extra = 10;
    return QRectF(sourcePoint_, QSizeF(targetPoint_.x() - sourcePoint_.x(), targetPoint_.y() - sourcePoint_.y()))
           .normalized()
           .adjusted(-extra, -extra, extra, extra);
}

void EdgeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    if (!source_ || !target_)
        return;

    QLineF line(sourcePoint_, targetPoint_);
    double angle = std::atan2(-line.dy(), line.dx());

    QPen linePen(QColor(180, 180, 200), 2.5, Qt::SolidLine, Qt::RoundCap);
    painter->setPen(linePen);
    painter->setBrush(Qt::NoBrush);

    QPainterPath curvePath(sourcePoint_);
    qreal dx = targetPoint_.x() - sourcePoint_.x();
    qreal dy = targetPoint_.y() - sourcePoint_.y();
    QPointF ctrl1(sourcePoint_.x() + dx * 0.5, sourcePoint_.y());
    QPointF ctrl2(targetPoint_.x() - dx * 0.5, targetPoint_.y());
    curvePath.cubicTo(ctrl1, ctrl2, targetPoint_);
    painter->drawPath(curvePath);

    QPointF arrowP1 = targetPoint_ - QPointF(sin(angle - M_PI / 6) * ARROW_SIZE, cos(angle - M_PI / 6) * ARROW_SIZE);
    QPointF arrowP2 = targetPoint_ - QPointF(sin(angle + M_PI / 6) * ARROW_SIZE, cos(angle + M_PI / 6) * ARROW_SIZE);

    painter->setPen(QPen(QColor(180, 180, 200), 1));
    painter->setBrush(QColor(200, 200, 220));
    painter->drawPolygon(QPolygonF({targetPoint_, arrowP1, arrowP2}));
}

int EdgeItem::type() const
{
    return Type;
}