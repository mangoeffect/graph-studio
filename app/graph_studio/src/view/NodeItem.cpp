#include "view/NodeItem.h"
#include "view/EdgeItem.h"
#include "viewmodel/GraphViewModel.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <algorithm>

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

QStringList NodeItem::inputPorts() const { return inputPorts_; }
QStringList NodeItem::outputPorts() const { return outputPorts_; }

void NodeItem::setPorts(const QStringList& inputs, const QStringList& outputs)
{
    inputPorts_ = inputs;
    outputPorts_ = outputs;
    update();
    notifyEdges();
}

void NodeItem::setRunStatus(RunStatus status)
{
    if (runStatus_ == status)
        return;
    runStatus_ = status;
    update();
}

NodeItem::RunStatus NodeItem::runStatus() const { return runStatus_; }

// 空端口列表退化为单个默认锚点，端口名 "in"/"out"
QStringList NodeItem::effectiveInput() const { return inputPorts_.isEmpty() ? QStringList{QStringLiteral("in")} : inputPorts_; }
QStringList NodeItem::effectiveOutput() const { return outputPorts_.isEmpty() ? QStringList{QStringLiteral("out")} : outputPorts_; }

qreal NodeItem::height() const
{
    const int n = std::max(effectiveInput().size(), effectiveOutput().size());
    return std::max(NODE_HEIGHT, qreal(PORT_HIT_RADIUS * 2 + 16 + n * 20));
}

// 端口中心在节点本地坐标。dir: 0=输入(左) 1=输出(右)
QPointF NodeItem::portLocalPos(int dir, int index, int count) const
{
    const qreal x = (dir == 0 ? -1.0 : 1.0) * NODE_WIDTH / 2;
    const qreal h = height();
    const qreal pad = PORT_HIT_RADIUS + 7;
    const qreal usable = h - 2 * pad;
    const qreal y = (count <= 1)
        ? 0
        : -usable / 2 + qreal(index) * usable / (count - 1);
    return QPointF(x, y);
}

bool NodeItem::hitPortAt(const QPointF& scenePos, int dir, const QStringList& ports, QString& name) const
{
    QPointF local = mapFromScene(scenePos);
    for (int i = 0; i < ports.size(); ++i) {
        if (QLineF(portLocalPos(dir, i, ports.size()), local).length() <= PORT_HIT_RADIUS) {
            name = ports[i];
            return true;
        }
    }
    return false;
}

NodeItem::PortDir NodeItem::hitPort(const QPointF& scenePos) const
{
    QString dummy;
    if (hitPortAt(scenePos, 0, effectiveInput(), dummy)) return PortDir::Input;
    if (hitPortAt(scenePos, 1, effectiveOutput(), dummy)) return PortDir::Output;
    return PortDir::None;
}

QString NodeItem::hitPortName(const QPointF& scenePos) const
{
    QString name;
    if (hitPortAt(scenePos, 0, effectiveInput(), name)) return name;
    if (hitPortAt(scenePos, 1, effectiveOutput(), name)) return name;
    return {};
}

QString NodeItem::inputPortNameAt(const QPointF& scenePos) const
{
    QString name;
    return hitPortAt(scenePos, 0, effectiveInput(), name) ? name : QString();
}

QString NodeItem::outputPortNameAt(const QPointF& scenePos) const
{
    QString name;
    return hitPortAt(scenePos, 1, effectiveOutput(), name) ? name : QString();
}

QPointF NodeItem::inputPortPos(const QString& name) const
{
    const QStringList ins = effectiveInput();
    int idx = ins.indexOf(name);
    if (idx < 0) idx = 0;
    return mapToScene(portLocalPos(0, idx, ins.size()));
}

QPointF NodeItem::outputPortPos(const QString& name) const
{
    const QStringList outs = effectiveOutput();
    int idx = outs.indexOf(name);
    if (idx < 0) idx = 0;
    return mapToScene(portLocalPos(1, idx, outs.size()));
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
    return QRectF(-NODE_WIDTH / 2 - margin, -height() / 2,
                  NODE_WIDTH + margin * 2, height());
}

QPainterPath NodeItem::shape() const
{
    QPainterPath path;
    path.addRoundedRect(QRectF(-NODE_WIDTH / 2, -height() / 2, NODE_WIDTH, height()), 8, 8);
    const auto ins = effectiveInput();
    for (int i = 0; i < ins.size(); ++i)
        path.addEllipse(portLocalPos(0, i, ins.size()), PORT_HIT_RADIUS, PORT_HIT_RADIUS);
    const auto outs = effectiveOutput();
    for (int i = 0; i < outs.size(); ++i)
        path.addEllipse(portLocalPos(1, i, outs.size()), PORT_HIT_RADIUS, PORT_HIT_RADIUS);
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
    PortDir p = hitPort(event->scenePos());
    if (p != hoverPort_) {
        hoverPort_ = p;
        setCursor(p == PortDir::None ? Qt::ArrowCursor : Qt::CrossCursor);
        update();
    }
    QGraphicsItem::hoverMoveEvent(event);
}

void NodeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    hoverPort_ = PortDir::None;
    setCursor(Qt::ArrowCursor);
    update();
    QGraphicsItem::hoverLeaveEvent(event);
}

void NodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(widget);

    QRectF bodyRect(-NODE_WIDTH / 2, -height() / 2, NODE_WIDTH, height());
    bool sel = option->state & QStyle::State_Selected;

    QColor bodyColor;
    QColor accentColor;
    const QString category = graph_studio::GraphViewModel::classifyTask(nodeType_);
    if (category == QStringLiteral("Input")) {
        bodyColor = QColor(40, 60, 80);
        accentColor = QColor(76, 175, 80);
    } else if (category == QStringLiteral("Output")) {
        bodyColor = QColor(80, 40, 40);
        accentColor = QColor(244, 67, 54);
    } else if (category.startsWith(QStringLiteral("OpenCV"))) {
        bodyColor = QColor(50, 55, 70);
        accentColor = QColor(92, 107, 192);
    } else if (category == QStringLiteral("GPU")) {
        bodyColor = QColor(40, 60, 60);
        accentColor = QColor(0, 188, 212);
    } else if (category == QStringLiteral("MediaPipe")) {
        bodyColor = QColor(60, 40, 60);
        accentColor = QColor(186, 104, 200);
    } else if (category == QStringLiteral("Scripting")) {
        bodyColor = QColor(60, 55, 40);
        accentColor = QColor(255, 193, 7);
    } else if (category == QStringLiteral("Color Grading")) {
        bodyColor = QColor(55, 40, 60);
        accentColor = QColor(255, 152, 0);
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
    QRectF accentRect(-NODE_WIDTH / 2, -height() / 2, 5, height());
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
    QRectF titleRect(-NODE_WIDTH / 2 + 12, -height() / 2 + 6, NODE_WIDTH - 20, 24);
    painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, nodeId_);

    // Type
    font.setBold(false);
    font.setPointSize(9);
    painter->setFont(font);
    QRectF typeRect(-NODE_WIDTH / 2 + 12, -height() / 2 + 30, NODE_WIDTH - 20, 18);
    painter->setPen(accentColor.lighter(130));
    painter->drawText(typeRect, Qt::AlignLeft | Qt::AlignVCenter, nodeType_);

    // ── Ports ──
    const auto ins = effectiveInput();
    const auto outs = effectiveOutput();

    for (int i = 0; i < ins.size(); ++i) {
        QPointF p = portLocalPos(0, i, ins.size());
        bool active = (hoverPort_ == PortDir::Input) || (dropHighlighted_ && i == ins.size() - 1);
        bool dropRing = dropHighlighted_ && i == ins.size() - 1;
        drawPort(painter, p, active, dropRing);
        drawPortLabel(painter, p, ins[i], true);
    }
    for (int i = 0; i < outs.size(); ++i) {
        QPointF p = portLocalPos(1, i, outs.size());
        bool active = (hoverPort_ == PortDir::Output);
        drawPort(painter, p, active, false);
        drawPortLabel(painter, p, outs[i], false);
    }
}

void NodeItem::drawPort(QPainter* painter, const QPointF& center, bool active, bool dropRing)
{
    qreal r = active ? PORT_RADIUS + 2 : PORT_RADIUS;
    QColor fill = active ? QColor(100, 200, 255) : QColor(70, 70, 75);
    QColor ring = active ? QColor(150, 220, 255) : QColor(120, 120, 120);

    if (dropRing) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(100, 200, 255, 60));
        painter->drawEllipse(center, PORT_RADIUS + 8, PORT_RADIUS + 8);
        painter->setBrush(QColor(100, 200, 255, 100));
        painter->drawEllipse(center, PORT_RADIUS + 4, PORT_RADIUS + 4);
    }

    painter->setPen(QPen(ring, 1.5));
    painter->setBrush(fill);
    painter->drawEllipse(center, r, r);
    painter->setPen(QPen(ring.lighter(130), 1.0));
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(center, r - 3, r - 3);
}

void NodeItem::drawPortLabel(QPainter* painter, const QPointF& center, const QString& name, bool input)
{
    QFont f = painter->font();
    f.setPointSize(7);
    f.setBold(true);
    painter->setFont(f);
    painter->setPen(QColor(200, 210, 225));
    QRectF labelRect;
    labelRect.setTopLeft(QPointF(center.x() + (input ? -84 : 4), center.y() - 8));
    labelRect.setSize(QSizeF(80, 16));
    painter->drawText(labelRect,
                      input ? Qt::AlignRight | Qt::AlignVCenter : Qt::AlignLeft | Qt::AlignVCenter,
                      name);
}

int NodeItem::type() const { return Type; }