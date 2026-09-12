#ifndef WGPU_IMAGE_VIEWER_H
#define WGPU_IMAGE_VIEWER_H

#include <QImage>
#include <QString>
#include <QWidget>

class QLabel;

namespace graph_studio {

// wgpu（WebGPU）surface 渲染的图像查看器：结果图面板的桌面 GPU 显示路径。
// 自持 wgpu instance/adapter/device/surface——不经 IGpuImageBackend（显示路径
// 与任务图后端分离，架构对位旧 GL 查看器自持 QOpenGLWidget 上下文）。
// zoom/pan/像素信息等交互行为与旧 GpuImageViewer 保持一致。
//
// 降级：wgpu surface/device 初始化失败（无适配器、Wayland 会话、编译未开
// TG_APP_HAS_WGPU 等）时，内部切换为 QLabel 位图视图（WASM 同款退化路径），
// 公共接口不变。WASM 构建不含本文件（CMake REMOVE_ITEM，与旧查看器一致）。
class WgpuImageViewer : public QWidget
{
    Q_OBJECT
public:
    explicit WgpuImageViewer(QWidget* parent = nullptr);
    ~WgpuImageViewer() override;

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
    struct Impl;
    Impl* impl_ = nullptr;

    // 视图状态（纯 widget 逻辑，与旧 GL 查看器逐字段相同；panY_ 沿用
    // "Y 向上 NDC" 约定——打包进 wgpu uniform 时取负，见实现注释）
    QImage image_;
    float zoom_ = 1.0f;
    float panX_ = 0.0f;
    float panY_ = 0.0f;
    bool dragging_ = false;
    QPointF lastDragPos_;

    // wgpu 初始化失败后的 QLabel 降级视图（惰性创建，覆盖整个 widget）
    bool wgpuFailed_ = false;
    QLabel* fallback_ = nullptr;

    bool ensureWgpu();
    void renderFrame();
    void enterFallbackMode();
    void updateFallback();
    void updatePixelInfo(const QPoint& mousePos);
    QPointF screenToImage(const QPointF& screenPos) const;
    void clampPan();
    void quadScale(float& sx, float& sy) const;

    static constexpr float MIN_ZOOM = 0.05f;
    static constexpr float MAX_ZOOM = 50.0f;
};

} // namespace graph_studio

#endif // WGPU_IMAGE_VIEWER_H
