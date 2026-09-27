#include "view/ImageViewer.h"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

using namespace graph_studio;

ImageViewer::ImageViewer(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::WheelFocus);  // WidgetWithChildrenShortcut 快捷键需要面板可持焦点
    setMinimumSize(200, 150);
}

void ImageViewer::quadScale(float& sx, float& sy) const {
    // Fit-to-view（contain）：zoom=1 时图像在保持纵横比下尽量填满视口。
    // screenToImage/clampPan/wheelEvent/paintEvent 共用。
    const float vw = static_cast<float>(width());
    const float vh = static_cast<float>(height());
    const float iw = static_cast<float>(image_.width());
    const float ih = static_cast<float>(image_.height());
    if (iw <= 0 || ih <= 0 || vw <= 0 || vh <= 0) {
        sx = sy = 1.0f;
        return;
    }
    const float imageAspect = iw / ih;
    const float viewportAspect = vw / vh;
    if (imageAspect > viewportAspect) {
        sx = zoom_;
        sy = (viewportAspect / imageAspect) * zoom_;
    } else {
        sx = (imageAspect / viewportAspect) * zoom_;
        sy = zoom_;
    }
}

void ImageViewer::setImage(const QImage& image) {
    image_ = image;
    resetView();
}

void ImageViewer::clearImage() {
    image_ = QImage();
    update();
}

void ImageViewer::resetView() {
    zoom_ = 1.0f;
    panX_ = 0.0f;
    panY_ = 0.0f;
    update();
    if (!image_.isNull())
        emit zoomChanged(currentMagnification());
}

void ImageViewer::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(20, 20, 20));  // #141414，与旧 GPU 查看器底色一致

    if (image_.isNull()) {
        p.setPen(QColor(120, 120, 120));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No image"));
        return;
    }

    // 图像占据的 NDC 矩形：角点 q=(±1,±1) → ndc = q*scale + pan；
    // NDC→widget：x=(ndcX+1)/2*w，y=(1-ndcY)/2*h（y=+1 为屏幕顶行）。
    float sx, sy;
    quadScale(sx, sy);
    const float w = static_cast<float>(width());
    const float h = static_cast<float>(height());
    const float leftNdc = -sx + panX_;
    const float rightNdc = sx + panX_;
    const float topNdc = sy + panY_;     // q.y=+1 → 图像上沿
    const float bottomNdc = -sy + panY_;
    QRectF target((leftNdc + 1.0f) * 0.5f * w,
                  (1.0f - topNdc) * 0.5f * h,
                  (rightNdc - leftNdc) * 0.5f * w,
                  (topNdc - bottomNdc) * 0.5f * h);

    p.setRenderHint(QPainter::SmoothPixmapTransform, zoom_ <= 4.0f);
    p.drawImage(target, image_);
}

void ImageViewer::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    // fit 放大率随视口尺寸变化，同步缩放标签
    if (!image_.isNull())
        emit zoomChanged(currentMagnification());
}

void ImageViewer::clampPan() {
    float sx, sy;
    quadScale(sx, sy);
    if (sx < 1.0f) {
        panX_ = 0.0f;
    } else {
        panX_ = std::clamp(panX_, -(sx - 1.0f), sx - 1.0f);
    }
    if (sy < 1.0f) {
        panY_ = 0.0f;
    } else {
        panY_ = std::clamp(panY_, -(sy - 1.0f), sy - 1.0f);
    }
}

QPointF ImageViewer::screenToImage(const QPointF& screenPos) const {
    if (image_.isNull()) return QPointF(-1, -1);

    const float vw = static_cast<float>(width());
    const float vh = static_cast<float>(height());
    const float iw = static_cast<float>(image_.width());
    const float ih = static_cast<float>(image_.height());

    float sx, sy;
    quadScale(sx, sy);

    // Screen -> NDC（quad 语义：y=+1 = 屏幕顶行）
    float ndcX = (2.0f * screenPos.x() / vw) - 1.0f;
    float ndcY = 1.0f - (2.0f * screenPos.y() / vh);

    // 与绘制同一映射：q = (ndc - pan) / scale，图像行号从图像上沿起
    float posNdcX = (ndcX - panX_) / sx;
    float posNdcY = (ndcY - panY_) / sy;

    float imgX = (posNdcX + 1.0f) * 0.5f * iw;
    float imgY = (1.0f - posNdcY) * 0.5f * ih;

    return QPointF(imgX, imgY);
}

void ImageViewer::updatePixelInfo(const QPoint& mousePos) {
    if (image_.isNull()) {
        emit pixelInfoChanged(QString());
        return;
    }

    QPointF imgPos = screenToImage(mousePos);
    int px = static_cast<int>(std::round(imgPos.x()));
    int py = static_cast<int>(std::round(imgPos.y()));

    if (px < 0 || px >= image_.width() || py < 0 || py >= image_.height()) {
        emit pixelInfoChanged(QString("x: -, y: - (outside image)"));
        return;
    }

    QColor c = image_.pixelColor(px, py);
    QString text;
    if (image_.format() == QImage::Format_Grayscale8 || image_.format() == QImage::Format_Grayscale16) {
        text = QString("x: %1, y: %2 | Gray: %3").arg(px).arg(py).arg(c.value());
    } else {
        text = QString("x: %1, y: %2 | R: %3 G: %4 B: %5%6")
                   .arg(px).arg(py)
                   .arg(c.red()).arg(c.green()).arg(c.blue())
                   .arg(c.alpha() < 255 ? QString(" A: %1").arg(c.alpha()) : QString());
    }
    emit pixelInfoChanged(text);
}

void ImageViewer::applyZoom(float newZoom, const QPointF& anchorNdc) {
    const float k = newZoom / zoom_;  // 实际生效比（newZoom 已 clamp，触界时饱和）

    // 锚定 anchorNdc 下的图像点：q = (ndc - pan) / scale 且 scale ∝ zoom，保持 q
    // 不变 ⇒ pan' = anchor·(1-k) + k·pan（精确闭式，x/y 同形）
    zoom_ = newZoom;
    panX_ = anchorNdc.x() * (1.0f - k) + k * panX_;
    panY_ = anchorNdc.y() * (1.0f - k) + k * panY_;

    clampPan();
    update();
    emit zoomChanged(currentMagnification());
}

float ImageViewer::currentMagnification() const {
    if (image_.isNull()) return 1.0f;
    float sx, sy;
    quadScale(sx, sy);
    const float w = static_cast<float>(width());
    const float iw = static_cast<float>(image_.width());
    if (w <= 0 || iw <= 0) return 1.0f;
    return sx * w / iw;  // 恒等于 sy*h/ih（quad 约定的纵横比锁定）
}

void ImageViewer::zoomIn() {
    if (image_.isNull()) return;
    applyZoom(std::clamp(zoom_ * 1.15f, MIN_ZOOM, MAX_ZOOM), QPointF(0, 0));
}

void ImageViewer::zoomOut() {
    if (image_.isNull()) return;
    applyZoom(std::clamp(zoom_ / 1.15f, MIN_ZOOM, MAX_ZOOM), QPointF(0, 0));
}

void ImageViewer::zoomTo1to1() {
    if (image_.isNull()) return;
    const float vw = static_cast<float>(width());
    const float vh = static_cast<float>(height());
    const float iw = static_cast<float>(image_.width());
    const float ih = static_cast<float>(image_.height());
    if (vw <= 0 || vh <= 0 || iw <= 0 || ih <= 0) return;
    // quad 约定下 x/y 放大率恒一致：zoom = max(iw/vw, ih/vh) 即屏幕像素 1:1
    applyZoom(std::clamp(std::max(iw / vw, ih / vh), MIN_ZOOM, MAX_ZOOM), QPointF(0, 0));
}

void ImageViewer::wheelEvent(QWheelEvent* event) {
    if (image_.isNull()) return;
    if (event->angleDelta().y() == 0) return;  // 纯水平滚动不缩放

    const QPointF mousePos = event->position();
    const float vw = static_cast<float>(width());
    const float vh = static_cast<float>(height());
    const QPointF anchorNdc(2.0f * mousePos.x() / vw - 1.0f,
                            1.0f - 2.0f * mousePos.y() / vh);

    const float factor = (event->angleDelta().y() > 0) ? 1.15f : 1.0f / 1.15f;
    applyZoom(std::clamp(zoom_ * factor, MIN_ZOOM, MAX_ZOOM), anchorNdc);
    updatePixelInfo(mousePos.toPoint());
}

void ImageViewer::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        lastDragPos_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void ImageViewer::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_) {
        QPointF delta = event->pos() - lastDragPos_;
        lastDragPos_ = event->pos();

        float vw = static_cast<float>(width());
        float vh = static_cast<float>(height());
        panX_ += 2.0f * delta.x() / vw;
        panY_ -= 2.0f * delta.y() / vh;
        clampPan();
        update();
    }
    updatePixelInfo(event->pos());
}

void ImageViewer::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = false;
        setCursor(Qt::ArrowCursor);
    }
}

void ImageViewer::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        resetView();
    }
}
