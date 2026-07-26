#include "view/NodeItem.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>

using namespace graph_studio;

const qreal NodeItem::NODE_WIDTH = 120;
const qreal NodeItem::NODE_HEIGHT = 60;
const qreal NodeItem::PORT_RADIUS = 6;

NodeItem::NodeItem(const QString& nodeId, const QString& nodeType, QGraphicsItem* parent)
    : QGraphicsItem(parent), nodeId_(nodeId), nodeType_(nodeType)
{
    setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemSendsGeometryChanges);
}

NodeItem::~NodeItem() = default;

QString NodeItem::nodeId() const
{
    return nodeId_;
}

QString NodeItem::nodeType() const
{
    return nodeType_;
}

void NodeItem::setSelected(bool selected)
{
    selected_ = selected;
    update();
}

bool NodeItem::isSelected() const
{
    return selected_;
}

QRectF NodeItem::boundingRect() const
{
    return QRectF(-NODE_WIDTH / 2, -NODE_HEIGHT / 2, NODE_WIDTH, NODE_HEIGHT);
}

void NodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(widget);

    QRectF rect = boundingRect();

    QColor fillColor = selected_ ? QColor(66, 133, 244) : QColor(245, 245, 245);
    QColor borderColor = selected_ ? QColor(52, 101, 164) : QColor(180, 180, 180);

    painter->setPen(QPen(borderColor, 2));
    painter->setBrush(fillColor);
    painter->drawRoundedRect(rect, 6, 6);

    painter->setPen(QPen(QColor(50, 50, 50), 1));
    painter->setBrush(Qt::NoBrush);

    QFont font = painter->font();
    font.setBold(true);
    font.setPointSize(11);
    painter->setFont(font);

    QRectF titleRect(-NODE_WIDTH / 2 + 10, -NODE_HEIGHT / 2 + 8, NODE_WIDTH - 20, 20);
    painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, nodeId_);

    font.setBold(false);
    font.setPointSize(9);
    painter->setFont(font);

    QColor typeColor;
    if (nodeType_ == "Input") {
        typeColor = QColor(76, 175, 80);
    } else if (nodeType_ == "Output") {
        typeColor = QColor(244, 67, 54);
    } else {
        typeColor = QColor(255, 193, 7);
    }

    QRectF typeRect(-NODE_WIDTH / 2 + 10, -NODE_HEIGHT / 2 + 32, NODE_WIDTH - 20, 16);
    painter->setPen(typeColor);
    painter->drawText(typeRect, Qt::AlignLeft | Qt::AlignVCenter, nodeType_);

    QPointF inputPort(-NODE_WIDTH / 2, 0);
    QPointF outputPort(NODE_WIDTH / 2, 0);

    painter->setPen(QPen(QColor(100, 100, 100), 1));
    painter->setBrush(QColor(200, 200, 200));
    painter->drawEllipse(inputPort, PORT_RADIUS, PORT_RADIUS);

    painter->setBrush(QColor(150, 150, 150));
    painter->drawEllipse(outputPort, PORT_RADIUS, PORT_RADIUS);
}

int NodeItem::type() const
{
    return Type;
}