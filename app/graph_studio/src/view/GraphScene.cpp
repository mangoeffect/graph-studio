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

    QColor gridColor(90, 90, 100, 80);
    QColor gridColorMajor(120, 120, 140, 120);
    QPen penMinor(gridColor, 1, Qt::SolidLine);
    QPen penMajor(gridColorMajor, 1, Qt::SolidLine);

    const int gridSize = 20;
    const int majorGridSize = 100;

    qreal left = int(rect.left()) - (int(rect.left()) % gridSize);
    qreal top = int(rect.top()) - (int(rect.top()) % gridSize);

    for (qreal x = left; x < rect.right(); x += gridSize) {
        painter->setPen(int(x) % majorGridSize == 0 ? penMajor : penMinor);
        painter->drawLine(x, rect.top(), x, rect.bottom());
    }

    for (qreal y = top; y < rect.bottom(); y += gridSize) {
        painter->setPen(int(y) % majorGridSize == 0 ? penMajor : penMinor);
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