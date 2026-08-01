#ifndef GPU_IMAGE_VIEWER_H
#define GPU_IMAGE_VIEWER_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QImage>
#include <QString>

namespace graph_studio {

class GpuImageViewer : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    explicit GpuImageViewer(QWidget* parent = nullptr);
    ~GpuImageViewer() override;

    void setImage(const QImage& image);
    void clearImage();

    QImage currentImage() const { return image_; }

    // Fit image to viewport (resets zoom/pan)
    void resetView();

signals:
    void pixelInfoChanged(const QString& text);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QImage image_;
    bool hasTexture_ = false;
    GLuint texture_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint program_ = 0;

    // Transform: zoom and pan
    float zoom_ = 1.0f;
    float panX_ = 0.0f;
    float panY_ = 0.0f;

    // Drag state
    bool dragging_ = false;
    QPointF lastDragPos_;

    // Shader uniform locations
    GLint loc_transform_ = -1;
    GLint loc_tex_ = -1;

    bool shaderCompiled_ = false;

    void compileShaders();
    void uploadTexture();
    void updatePixelInfo(const QPoint& mousePos);
    QPointF screenToImage(const QPointF& screenPos) const;
    void getTransformMatrix(float mat[9]) const;
    void clampPan();

    static constexpr float MIN_ZOOM = 0.05f;
    static constexpr float MAX_ZOOM = 50.0f;
};

} // namespace graph_studio

#endif // GPU_IMAGE_VIEWER_H
