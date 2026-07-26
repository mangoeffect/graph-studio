#include "view/GraphScene.h"

#include <QPainter>

using namespace graph_studio;

GraphScene::GraphScene(QObject* parent)
    : QGraphicsScene(parent)
{
    setSceneRect(-5000, -5000, 10000, 10000);
}

GraphScene::~GraphScene() = default;

void GraphScene::drawBackground(QPainter* painter, const QRectF& rect)
{
    QGraphicsScene::drawBackground(painter, rect);

    QColor gridColor(200, 200, 200, 100);
    QPen pen(gridColor, 1, Qt::SolidLine);
    painter->setPen(pen);

    const int gridSize = 20;

    qreal left = int(rect.left()) - (int(rect.left()) % gridSize);
    qreal top = int(rect.top()) - (int(rect.top()) % gridSize);

    for (qreal x = left; x < rect.right(); x += gridSize) {
        painter->drawLine(x, rect.top(), x, rect.bottom());
    }

    for (qreal y = top; y < rect.bottom(); y += gridSize) {
        painter->drawLine(rect.left(), y, rect.right(), y);
    }
}

void GraphScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    QGraphicsScene::mousePressEvent(event);
}

void GraphScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    QGraphicsScene::mouseMoveEvent(event);
}

void GraphScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    QGraphicsScene::mouseReleaseEvent(event);
}