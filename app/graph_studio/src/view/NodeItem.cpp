#include "view/NodeItem.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>

using namespace graph_studio;

const qreal NodeItem::NODE_WIDTH = 140;
const qreal NodeItem::NODE_HEIGHT = 70;
const qreal NodeItem::PORT_RADIUS = 7;

NodeItem::NodeItem(const QString& nodeId, const QString& nodeType, QGraphicsItem* parent)
    : QGraphicsItem(parent), nodeId_(nodeId), nodeType_(nodeType)
{
    setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemSendsGeometryChanges);
}

NodeItem::~NodeItem() = default;

QString NodeItem::nodeId() const { return nodeId_; }
QString NodeItem::nodeType() const { return nodeType_; }

void NodeItem::setSelected(bool selected)
{
    selected_ = selected;
    update();
}

bool NodeItem::isSelected() const { return selected_; }

QRectF NodeItem::boundingRect() const
{
    return QRectF(-NODE_WIDTH / 2, -NODE_HEIGHT / 2, NODE_WIDTH, NODE_HEIGHT);
}

void NodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(widget);

    QRectF rect = boundingRect();

    QColor bodyColor;
    QColor accentColor;
    if (nodeType_ == "Input") {
        bodyColor = QColor(40, 60, 80);
        accentColor = QColor(76, 175, 80);
    } else if (nodeType_ == "Output") {
        bodyColor = QColor(80, 40, 40);
        accentColor = QColor(244, 67, 54);
    } else {
        bodyColor = QColor(60, 55, 40);
        accentColor = QColor(255, 193, 7);
    }

    QColor fillColor = selected_ ? QColor(30, 90, 150) : bodyColor;
    QColor borderColor = selected_ ? QColor(100, 180, 255) : QColor(80, 80, 80);

    QPainterPath path;
    path.addRoundedRect(rect, 8, 8);

    QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());
    gradient.setColorAt(0, fillColor.lighter(115));
    gradient.setColorAt(1, fillColor);
    painter->fillPath(path, gradient);

    QPen borderPen(borderColor, selected_ ? 2.5 : 1.5);
    painter->setPen(borderPen);
    painter->drawPath(path);

    QRectF accentRect(-NODE_WIDTH / 2, -NODE_HEIGHT / 2, 5, NODE_HEIGHT);
    QPainterPath accentPath;
    accentPath.addRoundedRect(accentRect, 8, 8);
    accentPath = accentPath.intersected(path);
    painter->fillPath(accentPath, accentColor);

    painter->setPen(QPen(QColor(235, 235, 235), 1));
    QFont font = painter->font();
    font.setBold(true);
    font.setPointSize(10);
    painter->setFont(font);

    QRectF titleRect(-NODE_WIDTH / 2 + 12, -NODE_HEIGHT / 2 + 6, NODE_WIDTH - 20, 24);
    painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, nodeId_);

    font.setBold(false);
    font.setPointSize(9);
    painter->setFont(font);

    QRectF typeRect(-NODE_WIDTH / 2 + 12, -NODE_HEIGHT / 2 + 30, NODE_WIDTH - 20, 18);
    painter->setPen(accentColor.lighter(130));
    painter->drawText(typeRect, Qt::AlignLeft | Qt::AlignVCenter, nodeType_);

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