#include "view/EdgeItem.h"
#include "view/NodeItem.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsScene>
#include <cmath>

using namespace graph_studio;

const qreal EdgeItem::ARROW_SIZE = 10;

EdgeItem::EdgeItem(NodeItem* source, NodeItem* target, QGraphicsItem* parent)
    : QGraphicsItem(parent), source_(source), target_(target)
{
    setZValue(-1);
    setFlags(QGraphicsItem::ItemIsSelectable);
    setAcceptedMouseButtons(Qt::LeftButton);
    registerWithNodes();
    adjust();
}

EdgeItem::EdgeItem(NodeItem* source, const QPointF& freeEnd, QGraphicsItem* parent)
    : QGraphicsItem(parent), source_(source), isTemporary_(true), freeEnd_(freeEnd)
{
    setZValue(100); // on top during drag
    setAcceptedMouseButtons(Qt::NoButton);
    source_->registerEdge(this);
    adjust();
}

EdgeItem::~EdgeItem()
{
    unregisterFromNodes();
}

NodeItem* EdgeItem::sourceNode() const { return source_; }
NodeItem* EdgeItem::targetNode() const { return target_; }

void EdgeItem::setTargetNode(NodeItem* target)
{
    if (target_) {
        target_->unregisterEdge(this);
    }
    target_ = target;
    isTemporary_ = false;
    if (target_) {
        target_->registerEdge(this);
    }
    setZValue(-1);
    setFlags(QGraphicsItem::ItemIsSelectable);
    adjust();
}

void EdgeItem::setFreeEnd(const QPointF& pos)
{
    freeEnd_ = pos;
    adjust();
}

void EdgeItem::adjust()
{
    if (!source_)
        return;

    prepareGeometryChange();

    if (source_) {
        QRectF sourceRect = source_->boundingRect();
        sourcePoint_ = source_->mapToScene(QPointF(sourceRect.right(), sourceRect.center().y()));
    }

    if (target_) {
        QRectF targetRect = target_->boundingRect();
        targetPoint_ = target_->mapToScene(QPointF(targetRect.left(), targetRect.center().y()));
    } else if (isTemporary_) {
        targetPoint_ = freeEnd_;
    }
}

QRectF EdgeItem::boundingRect() const
{
    if (sourcePoint_ == targetPoint_)
        return QRectF();

    qreal extra = ARROW_SIZE + 5;
    return QRectF(sourcePoint_, QSizeF(targetPoint_.x() - sourcePoint_.x(),
                                       targetPoint_.y() - sourcePoint_.y()))
           .normalized()
           .adjusted(-extra, -extra, extra, extra);
}

void EdgeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(widget);

    if (!source_ && !isTemporary_)
        return;

    QLineF line(sourcePoint_, targetPoint_);

    QColor lineColor = isTemporary_ ? QColor(100, 180, 255, 180) : QColor(180, 180, 200);
    QColor arrowColor = isTemporary_ ? QColor(120, 200, 255) : QColor(200, 200, 220);

    if (option->state & QStyle::State_Selected) {
        lineColor = QColor(100, 200, 255);
    }

    if (line.length() < 1)
        return;

    double angle = std::atan2(-line.dy(), line.dx());

    QPen linePen(lineColor, isTemporary_ ? 2.0 : 2.5, Qt::SolidLine, Qt::RoundCap);
    painter->setPen(linePen);
    painter->setBrush(Qt::NoBrush);

    QPainterPath curvePath(sourcePoint_);
    qreal dx = targetPoint_.x() - sourcePoint_.x();
    qreal ctrlOffset = std::max(qreal(30), std::abs(dx) * 0.5);
    QPointF ctrl1(sourcePoint_.x() + ctrlOffset, sourcePoint_.y());
    QPointF ctrl2(targetPoint_.x() - ctrlOffset, targetPoint_.y());
    curvePath.cubicTo(ctrl1, ctrl2, targetPoint_);
    painter->drawPath(curvePath);

    if (!isTemporary_) {
        QPointF arrowP1 = targetPoint_ - QPointF(sin(angle - M_PI / 6) * ARROW_SIZE,
                                                  cos(angle - M_PI / 6) * ARROW_SIZE);
        QPointF arrowP2 = targetPoint_ - QPointF(sin(angle + M_PI / 6) * ARROW_SIZE,
                                                  cos(angle + M_PI / 6) * ARROW_SIZE);
        painter->setPen(QPen(lineColor, 1));
        painter->setBrush(arrowColor);
        painter->drawPolygon(QPolygonF({targetPoint_, arrowP1, arrowP2}));
    }
}

QVariant EdgeItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemSelectedHasChanged) {
        update();
    }
    return QGraphicsItem::itemChange(change, value);
}

QString EdgeItem::fromId() const
{
    return source_ ? source_->nodeId() : QString();
}

QString EdgeItem::toId() const
{
    return target_ ? target_->nodeId() : QString();
}

int EdgeItem::type() const { return Type; }

void EdgeItem::registerWithNodes()
{
    if (source_)
        source_->registerEdge(this);
    if (target_)
        target_->registerEdge(this);
}

void EdgeItem::unregisterFromNodes()
{
    if (source_)
        source_->unregisterEdge(this);
    if (target_)
        target_->unregisterEdge(this);
}
