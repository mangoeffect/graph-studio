#include "view/GraphScene.h"
#include "view/NodeItem.h"
#include "view/EdgeItem.h"

#include <QPainter>
#include <QGraphicsItem>
#include <QKeyEvent>

using namespace graph_studio;

GraphScene::GraphScene(QObject* parent)
    : QGraphicsScene(parent)
{
    setSceneRect(-5000, -5000, 10000, 10000);
    setItemIndexMethod(QGraphicsScene::NoIndex);
}

GraphScene::~GraphScene() = default;

NodeItem* GraphScene::findNodeItem(const QString& id) const
{
    for (auto* item : items()) {
        if (item->type() == NodeItem::Type) {
            auto* node = static_cast<NodeItem*>(item);
            if (node->nodeId() == id)
                return node;
        }
    }
    return nullptr;
}

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
    if (event->button() == Qt::LeftButton) {
        // Check if clicking on a node's output port to start edge creation
        auto* itemUnder = itemAt(event->scenePos(), QTransform());
        if (itemUnder && itemUnder->type() == NodeItem::Type) {
            auto* node = static_cast<NodeItem*>(itemUnder);
            auto port = node->hitPort(event->scenePos());
            if (port == NodeItem::Port::Output) {
                // Start edge drag
                portDragging_ = true;
                dragSource_ = node;
                tempEdge_ = new EdgeItem(node, event->scenePos());
                addItem(tempEdge_);
                event->accept();
                return;
            }
            if (port == NodeItem::Port::Input) {
                // Ignore - don't start node drag when clicking port
                event->accept();
                return;
            }
        }
    }
    QGraphicsScene::mousePressEvent(event);
}

void GraphScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (portDragging_ && tempEdge_) {
        // Snap to target node if hovering over input port
        bool snapped = false;
        for (auto* item : items(event->scenePos())) {
            if (item->type() == NodeItem::Type) {
                auto* node = static_cast<NodeItem*>(item);
                if (node != dragSource_ && node->hitPort(event->scenePos()) == NodeItem::Port::Input) {
                    tempEdge_->setFreeEnd(node->inputPortPos());
                    snapped = true;
                    break;
                }
            }
        }
        if (!snapped) {
            tempEdge_->setFreeEnd(event->scenePos());
        }
        event->accept();
        return;
    }
    QGraphicsScene::mouseMoveEvent(event);
}

void GraphScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (portDragging_ && event->button() == Qt::LeftButton) {
        // Find target node at release position
        NodeItem* targetNode = nullptr;
        for (auto* item : items(event->scenePos())) {
            if (item->type() == NodeItem::Type) {
                auto* node = static_cast<NodeItem*>(item);
                if (node != dragSource_ && node->hitPort(event->scenePos()) == NodeItem::Port::Input) {
                    targetNode = node;
                    break;
                }
            }
        }

        // Clean up temporary edge
        if (tempEdge_) {
            removeItem(tempEdge_);
            delete tempEdge_;
            tempEdge_ = nullptr;
        }

        if (targetNode) {
            emit edgeCreationRequested(dragSource_->nodeId(), targetNode->nodeId());
        }

        portDragging_ = false;
        dragSource_ = nullptr;
        event->accept();
        return;
    }

    // Notify node position changes after drag
    if (event->button() == Qt::LeftButton) {
        for (auto* item : selectedItems()) {
            if (item->type() == NodeItem::Type) {
                auto* node = static_cast<NodeItem*>(item);
                emit nodeMoved(node->nodeId(), node->pos().x(), node->pos().y());
            }
        }
    }

    QGraphicsScene::mouseReleaseEvent(event);
}

void GraphScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        auto* itemUnder = itemAt(event->scenePos(), QTransform());
        if (itemUnder && itemUnder->type() == NodeItem::Type) {
            auto* node = static_cast<NodeItem*>(itemUnder);
            emit nodeDoubleClicked(node->nodeId());
            event->accept();
            return;
        }
    }
    QGraphicsScene::mouseDoubleClickEvent(event);
}

void GraphScene::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (!selectedItems().isEmpty()) {
            // Deletion handled by MainWindow via selectionChanged
            // Just accept here to prevent default behavior
            event->accept();
            return;
        }
    }
    QGraphicsScene::keyPressEvent(event);
}
