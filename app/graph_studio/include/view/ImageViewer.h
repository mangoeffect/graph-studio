#ifndef IMAGE_VIEWER_H
#define IMAGE_VIEWER_H

#include <QImage>
#include <QString>
#include <QWidget>

namespace graph_studio {

// 图像结果查看器（纯 QPainter 绘制，桌面/WASM 同一代码路径）。
//
// 演进注记：曾改为 wgpu surface 直渲（WgpuImageViewer），但 macOS 26 +
// Qt 6.11 上 layer-hosting CAMetalLayer 嵌在 Qt layer-backed 树内根本不被
// 合成（wgpu acquire/draw/present 全部成功而内容不上屏，lldb 红背景实验
// 实证，详见 AGENTS.md wgpu 章节），已回退 CPU 绘制——结果本就是 CPU 侧
// QImage，显示路径无 GPU 增益损失。GPU 同步策略：结果收集只存原始输出
// （GPU 驻留纹理不下载），用户在结果下拉里选中某项时才 ensure_cpu 按需
// 下载（GraphViewModel::imageResult）。
//
// 视图数学（与 zoom/pan/像素信息共用的唯一约定）：
//   NDC quad：x∈[-1,1] 左→右，y=+1 为屏幕顶行；图像占据的 NDC 矩形为
//   q*scale + pan（q=图像归一化坐标，y=+1 对应图像上沿）。
class ImageViewer : public QWidget
{
    Q_OBJECT
public:
    explicit ImageViewer(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void clearImage();

    QImage currentImage() const { return image_; }

    // Fit image to viewport (resets zoom/pan)
    void resetView();

signals:
    void pixelInfoChanged(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QImage image_;
    float zoom_ = 1.0f;
    float panX_ = 0.0f;
    float panY_ = 0.0f;
    bool dragging_ = false;
    QPointF lastDragPos_;

    void updatePixelInfo(const QPoint& mousePos);
    QPointF screenToImage(const QPointF& screenPos) const;
    void clampPan();
    void quadScale(float& sx, float& sy) const;

    static constexpr float MIN_ZOOM = 0.05f;
    static constexpr float MAX_ZOOM = 50.0f;
};

} // namespace graph_studio

#endif // IMAGE_VIEWER_H
