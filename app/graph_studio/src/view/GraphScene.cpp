#include "view/GraphScene.h"
#include "view/NodeItem.h"
#include "view/EdgeItem.h"
#include "viewmodel/GraphViewModel.h"

#include <QPainter>
#include <QGraphicsItem>
#include <QKeyEvent>
#include <QMenu>
#include <QAction>
#include <QHash>

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
            QString outPort = node->outputPortNameAt(event->scenePos());
            if (!outPort.isEmpty()) {
                // Start edge drag from the specific output port
                portDragging_ = true;
                dragSource_ = node;
                dragSourcePort_ = outPort;
                tempEdge_ = new EdgeItem(node, event->scenePos(), outPort);
                addItem(tempEdge_);
                event->accept();
                return;
            }
            if (node->hitPort(event->scenePos()) == NodeItem::PortDir::Input) {
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
        // Snap to target node if hovering over input port.
        // Search all NodeItems directly (temp edge shape may mask target).
        bool snapped = false;
        NodeItem* newTarget = nullptr;
        for (auto* item : items()) {
            if (item->type() != NodeItem::Type)
                continue;
            auto* node = static_cast<NodeItem*>(item);
            if (node == dragSource_)
                continue;
            if (!node->inputPortNameAt(event->scenePos()).isEmpty()) {
                newTarget = node;
                tempEdge_->setFreeEnd(node->inputPortPos(node->inputPortNameAt(event->scenePos())));
                snapped = true;
                break;
            }
        }
        if (!snapped) {
            tempEdge_->setFreeEnd(event->scenePos());
        }

        // Update drop highlight
        if (newTarget != highlightedTarget_) {
            if (highlightedTarget_)
                highlightedTarget_->setDropHighlighted(false);
            highlightedTarget_ = newTarget;
            if (highlightedTarget_)
                highlightedTarget_->setDropHighlighted(true);
        }

        event->accept();
        return;
    }
    QGraphicsScene::mouseMoveEvent(event);
}

void GraphScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (portDragging_ && event->button() == Qt::LeftButton) {
        // Find target node at release position.
        // Don't rely on items(pos) because the temp edge's shape() may cover
        // the target and mask the NodeItem. Instead, search all NodeItems
        // and hit-test their input ports directly.
        NodeItem* targetNode = nullptr;
        QString targetPort;
        for (auto* item : items()) {
            if (item->type() != NodeItem::Type)
                continue;
            auto* node = static_cast<NodeItem*>(item);
            if (node == dragSource_)
                continue;
            QString inPort = node->inputPortNameAt(event->scenePos());
            if (!inPort.isEmpty()) {
                targetNode = node;
                targetPort = inPort;
                break;
            }
        }

        // Clean up temporary edge
        if (tempEdge_) {
            if (dragSource_) dragSource_->unregisterEdge(tempEdge_);
            removeItem(tempEdge_);
            delete tempEdge_;
            tempEdge_ = nullptr;
        }

        // Clear drop highlight
        if (highlightedTarget_) {
            highlightedTarget_->setDropHighlighted(false);
            highlightedTarget_ = nullptr;
        }

        if (targetNode) {
            emit edgeCreationRequested(dragSource_->nodeId(), dragSourcePort_,
                                       targetNode->nodeId(), targetPort);
        }

        portDragging_ = false;
        dragSource_ = nullptr;
        dragSourcePort_.clear();
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

void GraphScene::contextMenuEvent(QGraphicsSceneContextMenuEvent* event)
{
    // 右键空白处弹出任务创建菜单；右键节点时不拦截（交由默认行为）
    auto* itemUnder = itemAt(event->scenePos(), QTransform());
    if (itemUnder) {
        QGraphicsScene::contextMenuEvent(event);
        return;
    }

    QMenu menu;
    QPointF pos = event->scenePos();

    auto addSection = [&menu](const QString& title) {
        auto* a = menu.addAction(title);
        a->setEnabled(false);
        QFont f = a->font();
        f.setBold(true);
        a->setFont(f);
    };

    auto addTask = [this, &menu, pos](const QString& name) {
        menu.addAction(name, this, [this, name, pos]() {
            emit nodeCreateRequested(name, pos);
        });
    };

    // 从 ViewModel 设置的可用 task 类型（不直接访问 PluginRegistry）
    QStringList allTypes = availableTaskTypes_;
    allTypes.sort();
    if (allTypes.isEmpty()) {
        menu.addAction("(no tasks registered)")->setEnabled(false);
        menu.exec(event->screenPos());
        event->accept();
        return;
    }
    auto classify = [](const QString& type) -> QString {
        return graph_studio::GraphViewModel::classifyTask(type);
    };
    QStringList sections;
    QHash<QString, QStringList> bySection;
    for (const auto& t : allTypes) {
        const QString s = classify(t);
        if (!bySection.contains(s)) sections.append(s);
        bySection[s].append(t);
    }
    for (int i = 0; i < sections.size(); ++i) {
        if (i > 0) menu.addSeparator();
        addSection(sections[i]);
        for (const auto& t : bySection[sections[i]]) addTask(t);
    }

    menu.exec(event->screenPos());
    event->accept();
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
