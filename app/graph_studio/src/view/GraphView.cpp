#include "view/GraphView.h"

#include <QWheelEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>

using namespace graph_studio;

namespace {

// 从 mime 数据里取第一个指向本地 .json 文件的路径，无则返回空。
QString firstJsonPath(const QMimeData* mime)
{
    if (!mime || !mime->hasUrls()) return {};
    const auto urls = mime->urls();
    for (const QUrl& url : urls) {
        if (!url.isLocalFile()) continue;
        const QString path = url.toLocalFile();
        if (QFileInfo(path).suffix().compare("json", Qt::CaseInsensitive) == 0)
            return path;
    }
    return {};
}

bool hasJsonDrop(const QMimeData* mime)
{
    return !firstJsonPath(mime).isEmpty();
}

} // namespace

GraphView::GraphView(QGraphicsScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
{
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::RubberBandDrag);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setMinimumWidth(400);
    setAcceptDrops(true);
}

GraphView::~GraphView() = default;

void GraphView::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const qreal factor = (event->angleDelta().y() > 0) ? 1.15 : 1.0 / 1.15;
        scale(factor, factor);
        emit zoomChanged(transform().m11());
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

void GraphView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        panStart_ = event->pos();
        panOrigin_ = QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value());
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void GraphView::mouseMoveEvent(QMouseEvent* event)
{
    if (panning_) {
        QPointF delta = event->pos() - panStart_;
        horizontalScrollBar()->setValue(int(panOrigin_.x() - delta.x()));
        verticalScrollBar()->setValue(int(panOrigin_.y() - delta.y()));
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void GraphView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton && panning_) {
        panning_ = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void GraphView::dragEnterEvent(QDragEnterEvent* event)
{
    if (hasJsonDrop(event->mimeData())) {
        event->acceptProposedAction();
        return;
    }
    if (event->mimeData()->hasText()) {
        event->acceptProposedAction();
        return;
    }
    QGraphicsView::dragEnterEvent(event);
}

void GraphView::dragMoveEvent(QDragMoveEvent* event)
{
    if (hasJsonDrop(event->mimeData())) {
        event->acceptProposedAction();
        return;
    }
    if (event->mimeData()->hasText()) {
        event->acceptProposedAction();
        return;
    }
    QGraphicsView::dragMoveEvent(event);
}

void GraphView::dropEvent(QDropEvent* event)
{
    // 文件拖入优先于文本：Finder/文件管理器拖 .json 时常同时携带 text/plain，
    // 若先判 hasText() 会把文件路径误当 task 类型建节点。
    const QString jsonPath = firstJsonPath(event->mimeData());
    if (!jsonPath.isEmpty()) {
        emit graphFileDropped(jsonPath);
        event->acceptProposedAction();
        return;
    }
    if (event->mimeData()->hasText()) {
        QString taskType = event->mimeData()->text();
        QPointF scenePos = mapToScene(event->position().toPoint());
        emit taskDropped(taskType, scenePos);
        event->acceptProposedAction();
        return;
    }
    QGraphicsView::dropEvent(event);
}
