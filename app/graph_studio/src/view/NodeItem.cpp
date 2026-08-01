#include "view/NodeItem.h"
#include "view/EdgeItem.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>

using namespace graph_studio;

const qreal NodeItem::NODE_WIDTH = 140;
const qreal NodeItem::NODE_HEIGHT = 70;
const qreal NodeItem::PORT_RADIUS = 7;
const qreal NodeItem::PORT_HIT_RADIUS = 14;

NodeItem::NodeItem(const QString& nodeId, const QString& nodeType, QGraphicsItem* parent)
    : QGraphicsItem(parent), nodeId_(nodeId), nodeType_(nodeType)
{
    setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
}

NodeItem::~NodeItem()
{
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
    QPointF local = mapFromScene(scenePos);
    QPointF inputCenter(-NODE_WIDTH / 2, 0);
    if (QLineF(inputCenter, local).length() <= PORT_HIT_RADIUS)
        return Port::Input;

    QPointF outputCenter(NODE_WIDTH / 2, 0);
    if (QLineF(outputCenter, local).length() <= PORT_HIT_RADIUS)
        return Port::Output;

    return Port::None;
}

void NodeItem::setDropHighlighted(bool on)
{
    if (dropHighlighted_ == on)
        return;
    dropHighlighted_ = on;
    update();
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
    qreal margin = PORT_HIT_RADIUS + 2;
    return QRectF(-NODE_WIDTH / 2 - margin, -NODE_HEIGHT / 2,
                  NODE_WIDTH + margin * 2, NODE_HEIGHT);
}

QPainterPath NodeItem::shape() const
{
    QPainterPath path;
    // Node body
    path.addRoundedRect(QRectF(-NODE_WIDTH / 2, -NODE_HEIGHT / 2, NODE_WIDTH, NODE_HEIGHT), 8, 8);
    // Port hit areas (circles extending beyond the body edges)
    path.addEllipse(QPointF(-NODE_WIDTH / 2, 0), PORT_HIT_RADIUS, PORT_HIT_RADIUS);
    path.addEllipse(QPointF(NODE_WIDTH / 2, 0), PORT_HIT_RADIUS, PORT_HIT_RADIUS);
    return path.simplified();
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

void NodeItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
    Port p = hitPort(event->scenePos());
    if (p != hoverPort_) {
        hoverPort_ = p;
        if (p == Port::None) {
            setCursor(Qt::ArrowCursor);
        } else {
            setCursor(Qt::CrossCursor);
        }
        update();
    }
    QGraphicsItem::hoverMoveEvent(event);
}

void NodeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    hoverPort_ = Port::None;
    setCursor(Qt::ArrowCursor);
    update();
    QGraphicsItem::hoverLeaveEvent(event);
}

void NodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(widget);

    QRectF bodyRect(-NODE_WIDTH / 2, -NODE_HEIGHT / 2, NODE_WIDTH, NODE_HEIGHT);
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

    switch (runStatus_) {
        case RunStatus::Running:   borderColor = QColor(66, 165, 245); borderWidth = 3.0; break;
        case RunStatus::Completed: borderColor = QColor(102, 187, 106); borderWidth = 3.0; break;
        case RunStatus::Failed:    borderColor = QColor(239, 83, 80);  borderWidth = 3.0; break;
        case RunStatus::None:      break;
    }

    QPainterPath path;
    path.addRoundedRect(bodyRect, 8, 8);

    QLinearGradient gradient(bodyRect.topLeft(), bodyRect.bottomLeft());
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

    // ── Ports ──
    QPointF inputPort(-NODE_WIDTH / 2, 0);
    QPointF outputPort(NODE_WIDTH / 2, 0);

    bool inHover = (hoverPort_ == Port::Input);
    bool outHover = (hoverPort_ == Port::Output);

    // Input port
    if (dropHighlighted_) {
        // Glow ring for drop target
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(100, 200, 255, 60));
        painter->drawEllipse(inputPort, PORT_RADIUS + 8, PORT_RADIUS + 8);
        painter->setBrush(QColor(100, 200, 255, 100));
        painter->drawEllipse(inputPort, PORT_RADIUS + 4, PORT_RADIUS + 4);
    }

    qreal inR = inHover || dropHighlighted_ ? PORT_RADIUS + 2 : PORT_RADIUS;
    QColor inFill = inHover || dropHighlighted_ ? QColor(100, 200, 255) : QColor(60, 60, 60);
    QColor inRing = inHover || dropHighlighted_ ? QColor(150, 220, 255) : QColor(120, 120, 120);

    painter->setPen(QPen(inRing, 1.5));
    painter->setBrush(inFill);
    painter->drawEllipse(inputPort, inR, inR);

    painter->setPen(QPen(inRing.lighter(130), 1.0));
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(inputPort, inR - 3, inR - 3);

    // Output port
    qreal outR = outHover ? PORT_RADIUS + 2 : PORT_RADIUS;
    QColor outFill = outHover ? QColor(100, 200, 255) : QColor(80, 80, 80);
    QColor outRing = outHover ? QColor(150, 220, 255) : QColor(120, 120, 120);

    painter->setPen(QPen(outRing, 1.5));
    painter->setBrush(outFill);
    painter->drawEllipse(outputPort, outR, outR);

    painter->setPen(QPen(outRing.lighter(130), 1.0));
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(outputPort, outR - 3, outR - 3);
}

int NodeItem::type() const { return Type; }
