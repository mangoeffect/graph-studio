#include "view/NodeItem.h"
#include "view/EdgeItem.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsScene>

using namespace graph_studio;

const qreal NodeItem::NODE_WIDTH = 140;
const qreal NodeItem::NODE_HEIGHT = 70;
const qreal NodeItem::PORT_RADIUS = 7;

NodeItem::NodeItem(const QString& nodeId, const QString& nodeType, QGraphicsItem* parent)
    : QGraphicsItem(parent), nodeId_(nodeId), nodeType_(nodeType)
{
    setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
}

NodeItem::~NodeItem()
{
    // 不在析构中操作 scene 或 edge 对象：QGraphicsScene::clear() 批量销毁
    // items 时析构顺序不确定，交叉访问会导致 UAF。edge 的删除由
    // MainWindow 在运行时统一管理（先删 edge 再删 node）。
    edges_.clear();
}

QString NodeItem::nodeId() const { return nodeId_; }
QString NodeItem::nodeType() const { return nodeType_; }
void NodeItem::setNodeType(const QString& type) { nodeType_ = type; update(); }

void NodeItem::setRunStatus(RunStatus status)
{
    if (runStatus_ == status)
        return;
    runStatus_ = status;
    update();
}

NodeItem::RunStatus NodeItem::runStatus() const { return runStatus_; }

QPointF NodeItem::inputPortPos() const
{
    return mapToScene(QPointF(-NODE_WIDTH / 2, 0));
}

QPointF NodeItem::outputPortPos() const
{
    return mapToScene(QPointF(NODE_WIDTH / 2, 0));
}

NodeItem::Port NodeItem::hitPort(const QPointF& scenePos) const
{
    QPointF inputLocal = mapFromScene(scenePos);
    // Check input port (left side)
    QPointF inputCenter(-NODE_WIDTH / 2, 0);
    qreal hitRadius = PORT_RADIUS + 6; // generous hit area
    if (QLineF(inputCenter, inputLocal).length() <= hitRadius)
        return Port::Input;

    QPointF outputCenter(NODE_WIDTH / 2, 0);
    if (QLineF(outputCenter, inputLocal).length() <= hitRadius)
        return Port::Output;

    return Port::None;
}

void NodeItem::registerEdge(EdgeItem* edge)
{
    edges_.insert(edge);
}

void NodeItem::unregisterEdge(EdgeItem* edge)
{
    edges_.remove(edge);
}

QRectF NodeItem::boundingRect() const
{
    return QRectF(-NODE_WIDTH / 2, -NODE_HEIGHT / 2, NODE_WIDTH, NODE_HEIGHT);
}

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionHasChanged) {
        notifyEdges();
    }
    if (change == ItemSelectedHasChanged) {
        update();
    }
    return QGraphicsItem::itemChange(change, value);
}

void NodeItem::notifyEdges()
{
    for (auto* edge : edges_) {
        if (edge)
            edge->adjust();
    }
}

void NodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(widget);

    QRectF rect = boundingRect();
    bool sel = option->state & QStyle::State_Selected;

    QColor bodyColor;
    QColor accentColor;
    if (nodeType_.contains("input", Qt::CaseInsensitive)) {
        bodyColor = QColor(40, 60, 80);
        accentColor = QColor(76, 175, 80);
    } else if (nodeType_.contains("output", Qt::CaseInsensitive) ||
               nodeType_.contains("display", Qt::CaseInsensitive) ||
               nodeType_.contains("save", Qt::CaseInsensitive)) {
        bodyColor = QColor(80, 40, 40);
        accentColor = QColor(244, 67, 54);
    } else {
        bodyColor = QColor(60, 55, 40);
        accentColor = QColor(255, 193, 7);
    }

    QColor fillColor = sel ? QColor(30, 90, 150) : bodyColor;
    QColor borderColor = sel ? QColor(100, 180, 255) : QColor(80, 80, 80);
    qreal borderWidth = sel ? 2.5 : 1.5;

    // 执行状态优先覆盖边框：运行中蓝、成功绿、失败红。
    switch (runStatus_) {
        case RunStatus::Running:   borderColor = QColor(66, 165, 245); borderWidth = 3.0; break;
        case RunStatus::Completed: borderColor = QColor(102, 187, 106); borderWidth = 3.0; break;
        case RunStatus::Failed:    borderColor = QColor(239, 83, 80);  borderWidth = 3.0; break;
        case RunStatus::None:      break;
    }

    QPainterPath path;
    path.addRoundedRect(rect, 8, 8);

    QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());
    gradient.setColorAt(0, fillColor.lighter(115));
    gradient.setColorAt(1, fillColor);
    painter->fillPath(path, gradient);

    QPen borderPen(borderColor, borderWidth);
    painter->setPen(borderPen);
    painter->drawPath(path);

    // Accent bar on left
    QRectF accentRect(-NODE_WIDTH / 2, -NODE_HEIGHT / 2, 5, NODE_HEIGHT);
    QPainterPath accentPath;
    accentPath.addRoundedRect(accentRect, 8, 8);
    accentPath = accentPath.intersected(path);
    painter->fillPath(accentPath, accentColor);

    // Title
    painter->setPen(QPen(QColor(235, 235, 235), 1));
    QFont font = painter->font();
    font.setBold(true);
    font.setPointSize(10);
    painter->setFont(font);
    QRectF titleRect(-NODE_WIDTH / 2 + 12, -NODE_HEIGHT / 2 + 6, NODE_WIDTH - 20, 24);
    painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, nodeId_);

    // Type
    font.setBold(false);
    font.setPointSize(9);
    painter->setFont(font);
    QRectF typeRect(-NODE_WIDTH / 2 + 12, -NODE_HEIGHT / 2 + 30, NODE_WIDTH - 20, 18);
    painter->setPen(accentColor.lighter(130));
    painter->drawText(typeRect, Qt::AlignLeft | Qt::AlignVCenter, nodeType_);

    // Ports
    QPointF inputPort(-NODE_WIDTH / 2, 0);
    QPointF outputPort(NODE_WIDTH / 2, 0);

    painter->setPen(QPen(QColor(120, 120, 120), 1.5));
    painter->setBrush(QColor(60, 60, 60));
    painter->drawEllipse(inputPort, PORT_RADIUS, PORT_RADIUS);

    painter->setBrush(QColor(80, 80, 80));
    painter->drawEllipse(outputPort, PORT_RADIUS, PORT_RADIUS);

    painter->setPen(QPen(QColor(180, 180, 180), 1.5));
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(inputPort, PORT_RADIUS - 2, PORT_RADIUS - 2);
    painter->drawEllipse(outputPort, PORT_RADIUS - 2, PORT_RADIUS - 2);
}

int NodeItem::type() const { return Type; }
