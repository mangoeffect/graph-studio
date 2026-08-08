#include "view/EdgeItem.h"
#include "view/NodeItem.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsScene>
#include <cmath>

using namespace graph_studio;

const qreal EdgeItem::ARROW_SIZE = 10;

EdgeItem::EdgeItem(NodeItem* source, NodeItem* target,
                   const QString& sourcePort, const QString& targetPort,
                   QGraphicsItem* parent)
    : QGraphicsItem(parent), source_(source), target_(target),
      sourcePort_(sourcePort), targetPort_(targetPort)
{
    setZValue(-1);
    setFlags(QGraphicsItem::ItemIsSelectable);
    setAcceptedMouseButtons(Qt::LeftButton);
    registerWithNodes();
    adjust();
}

EdgeItem::EdgeItem(NodeItem* source, const QPointF& freeEnd,
                   const QString& sourcePort,
                   QGraphicsItem* parent)
    : QGraphicsItem(parent), source_(source), isTemporary_(true),
      freeEnd_(freeEnd), sourcePort_(sourcePort)
{
    setZValue(100); // on top during drag
    setAcceptedMouseButtons(Qt::NoButton);
    source_->registerEdge(this);
    adjust();
}

EdgeItem::~EdgeItem()
{
    // 不访问 source_/target_：scene 批量销毁 items 时它们可能已析构。
    // 运行时单个 edge 删除由 MainWindow 调用 unregisterEdge 显式维护关系。
    source_ = nullptr;
    target_ = nullptr;
}

NodeItem* EdgeItem::sourceNode() const { return source_; }
NodeItem* EdgeItem::targetNode() const { return target_; }
QString EdgeItem::sourcePort() const { return sourcePort_; }
QString EdgeItem::targetPort() const { return targetPort_; }

void EdgeItem::setTargetNode(NodeItem* target, const QString& targetPort)
{
    if (target_) {
        target_->unregisterEdge(this);
    }
    target_ = target;
    targetPort_ = targetPort;
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
        sourcePoint_ = source_->outputPortPos(sourcePort_.isEmpty() ? QStringLiteral("out") : sourcePort_);
    }

    if (target_) {
        targetPoint_ = target_->inputPortPos(targetPort_.isEmpty() ? QStringLiteral("in") : targetPort_);
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
