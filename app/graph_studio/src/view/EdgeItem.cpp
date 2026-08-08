#include "view/EdgeItem.h"
#include "view/NodeItem.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsScene>
#include <QHash>
#include <QFontMetricsF>
#include <cmath>

using namespace graph_studio;

const qreal EdgeItem::ARROW_SIZE = 10;

EdgeItem::EdgeItem(NodeItem* source, NodeItem* target,
                   const QString& sourcePort, const QString& targetPort,
                   QGraphicsItem* parent)
    : QGraphicsItem(parent), source_(source), target_(target),
      sourcePort_(sourcePort), targetPort_(targetPort), bowOffset_(edgeBowOffset())
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

    // 额外留出标签（halo + 字号）与 bow 弯曲的高度
    qreal extra = ARROW_SIZE + 5 + 18;
    return QRectF(sourcePoint_, QSizeF(targetPoint_.x() - sourcePoint_.x(),
                                       targetPoint_.y() - sourcePoint_.y()))
           .normalized()
           .adjusted(-extra, -extra, extra, extra);
}

// 确定性 y 向 bow：让同一对节点之间、不同端口的边曲线相互错开，标签不重叠。
qreal EdgeItem::edgeBowOffset() const
{
    uint h = qHash(sourcePort_);
    h = qHash(targetPort_, h);
    const qreal amt = 18.0 + static_cast<qreal>(h % 3) * 10.0;  // 18 ~ 38
    return ((h >> 3) & 1u) ? amt : -amt;
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
    QPointF ctrl1(sourcePoint_.x() + ctrlOffset, sourcePoint_.y() + (isTemporary_ ? 0 : bowOffset_));
    QPointF ctrl2(targetPoint_.x() - ctrlOffset, targetPoint_.y() + (isTemporary_ ? 0 : bowOffset_));
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

    // 端口名标签（同 pair 多端口边靠标签区分）
    if (!isTemporary_)
        paintLabel(painter, option);
}

void EdgeItem::paintLabel(QPainter* painter, const QStyleOptionGraphicsItem* option) const
{
    // 缩放过小时隐藏标签，避免文字糊成一团
    qreal lod = option->levelOfDetailFromTransform(painter->worldTransform());
    if (lod < 0.4)
        return;

    const QString src = sourcePort_.isEmpty() ? QStringLiteral("out") : sourcePort_;
    const QString dst = targetPort_.isEmpty() ? QStringLiteral("in") : targetPort_;
    const QString label = src + QStringLiteral(" → ") + dst;

    // cubic B(t=0.5) 中点：0.125 P0 + 0.375 C1 + 0.375 C2 + 0.125 P3
    qreal dx = targetPoint_.x() - sourcePoint_.x();
    qreal ctrlOffset = std::max(qreal(30), std::abs(dx) * 0.5);
    QPointF c1(sourcePoint_.x() + ctrlOffset, sourcePoint_.y() + bowOffset_);
    QPointF c2(targetPoint_.x() - ctrlOffset, targetPoint_.y() + bowOffset_);
    QPointF mid = sourcePoint_ * 0.125 + c1 * 0.375 + c2 * 0.375 + targetPoint_ * 0.125;
    // 略微向上抬，让标签浮在曲线上方，避免被箭头/曲线压住
    mid.setY(mid.y() - 6);

    QFont f = painter->font();
    f.setPointSize(7);
    painter->setFont(f);

    QFontMetricsF fm = painter->fontMetrics();
    QRectF lr(mid.x() - fm.horizontalAdvance(label) / 2 - 5,
              mid.y() - fm.height() / 2 - 1,
              fm.horizontalAdvance(label) + 10, fm.height() + 2);

    // 深色 halo：对暗色背景的反差
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(25, 25, 30, 200));
    painter->drawRoundedRect(lr, 4, 4);

    painter->setPen(QPen(QColor(215, 230, 255), 1));
    painter->drawText(lr, Qt::AlignCenter, label);
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
