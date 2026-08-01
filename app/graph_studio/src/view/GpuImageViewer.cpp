#include "view/GpuImageViewer.h"

#include <QWheelEvent>
#include <QMouseEvent>
#include <QPainter>
#include <cmath>

using namespace graph_studio;

static const char* kVertSrc = R"(#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
uniform mat3 u_transform;
out vec2 v_uv;
void main() {
    vec3 p = u_transform * vec3(a_pos, 1.0);
    gl_Position = vec4(p.xy, 0.0, 1.0);
    v_uv = a_uv;
}
)";

static const char* kFragSrc = R"(#version 330 core
precision mediump float;
in vec2 v_uv;
uniform sampler2D u_tex;
out vec4 fragColor;
void main() {
    if (v_uv.x < 0.0 || v_uv.x > 1.0 || v_uv.y < 0.0 || v_uv.y > 1.0)
        discard;
    fragColor = texture(u_tex, v_uv);
}
)";

GpuImageViewer::GpuImageViewer(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(200, 150);
}

GpuImageViewer::~GpuImageViewer()
{
    if (hasTexture_) {
        makeCurrent();
        if (texture_) glDeleteTextures(1, &texture_);
        if (vao_) glDeleteVertexArrays(1, &vao_);
        if (vbo_) glDeleteBuffers(1, &vbo_);
        if (program_) glDeleteProgram(program_);
        doneCurrent();
    }
}

void GpuImageViewer::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.08f, 0.08f, 0.08f, 1.0f);  // #141414
    compileShaders();

    // Quad: position (x,y) + uv (u,v)
    float verts[] = {
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
    };

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);

    if (!image_.isNull()) uploadTexture();
}

void GpuImageViewer::compileShaders()
{
    if (shaderCompiled_) return;

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &kVertSrc, nullptr);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &kFragSrc, nullptr);
    glCompileShader(fs);

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);

    glDeleteShader(vs);
    glDeleteShader(fs);

    loc_transform_ = glGetUniformLocation(program_, "u_transform");
    loc_tex_ = glGetUniformLocation(program_, "u_tex");

    shaderCompiled_ = true;
}

void GpuImageViewer::uploadTexture()
{
    if (image_.isNull()) return;

    // Convert to RGBA8888 for GL upload
    QImage glImg = image_.convertToFormat(QImage::Format_RGBA8888);

    if (!hasTexture_) {
        glGenTextures(1, &texture_);
        hasTexture_ = true;
    }

    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 glImg.width(), glImg.height(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, glImg.constBits());
}

void GpuImageViewer::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void GpuImageViewer::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);

    if (image_.isNull() || !hasTexture_) {
        QPainter p(this);
        p.setPen(QColor(120, 120, 120));
        p.drawText(rect(), Qt::AlignCenter, "No image");
        return;
    }

    glUseProgram(program_);

    float mat[9];
    getTransformMatrix(mat);
    glUniformMatrix3fv(loc_transform_, 1, GL_FALSE, mat);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glUniform1i(loc_tex_, 0);

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

void GpuImageViewer::getTransformMatrix(float mat[9]) const
{
    float vw = static_cast<float>(width());
    float vh = static_cast<float>(height());
    float iw = static_cast<float>(image_.width());
    float ih = static_cast<float>(image_.height());

    float imageAspect = iw / ih;
    float viewportAspect = vw / vh;

    // Fit-to-view (contain): at zoom=1.0 the image fills the viewport
    // as much as possible while preserving aspect ratio.
    float quadScaleX, quadScaleY;
    if (imageAspect > viewportAspect) {
        // Image is wider: fit to width
        quadScaleX = zoom_;
        quadScaleY = (viewportAspect / imageAspect) * zoom_;
    } else {
        // Image is taller: fit to height
        quadScaleX = (imageAspect / viewportAspect) * zoom_;
        quadScaleY = zoom_;
    }

    // mat3 is column-major in OpenGL:
    // [ sx  0   tx ]
    // [ 0  -sy  ty ]   (negative sy because GL Y is up, image Y is down)
    // [ 0   0   1  ]
    mat[0] = quadScaleX;  mat[3] = 0.0f;        mat[6] = panX_;
    mat[1] = 0.0f;        mat[4] = -quadScaleY;  mat[7] = panY_;
    mat[2] = 0.0f;        mat[5] = 0.0f;        mat[8] = 1.0f;
}

void GpuImageViewer::setImage(const QImage& image)
{
    image_ = image;
    if (!image_.isNull()) {
        // Upload texture if GL context is ready (initializeGL has run).
        // If context isn't ready yet, initializeGL will upload when it runs.
        if (context() && context()->isValid()) {
            makeCurrent();
            uploadTexture();
            doneCurrent();
        }
    }
    resetView();
    update();
}

void GpuImageViewer::clearImage()
{
    image_ = QImage();
    update();
}

void GpuImageViewer::resetView()
{
    zoom_ = 1.0f;
    panX_ = 0.0f;
    panY_ = 0.0f;
    update();
}

void GpuImageViewer::clampPan()
{
    float vw = static_cast<float>(width());
    float vh = static_cast<float>(height());
    float iw = static_cast<float>(image_.width());
    float ih = static_cast<float>(image_.height());
    float imageAspect = iw / ih;
    float viewportAspect = vw / vh;

    // Compute displayed quad size in NDC (same logic as getTransformMatrix)
    float quadScaleX, quadScaleY;
    if (imageAspect > viewportAspect) {
        quadScaleX = zoom_;
        quadScaleY = (viewportAspect / imageAspect) * zoom_;
    } else {
        quadScaleX = (imageAspect / viewportAspect) * zoom_;
        quadScaleY = zoom_;
    }

    // quadScaleX/Y are in NDC [-1,1]. If < 1.0, image fits within viewport.
    if (quadScaleX < 1.0f) {
        panX_ = 0.0f;
    } else {
        panX_ = std::clamp(panX_, -(quadScaleX - 1.0f), quadScaleX - 1.0f);
    }
    if (quadScaleY < 1.0f) {
        panY_ = 0.0f;
    } else {
        panY_ = std::clamp(panY_, -(quadScaleY - 1.0f), quadScaleY - 1.0f);
    }
}

QPointF GpuImageViewer::screenToImage(const QPointF& screenPos) const
{
    if (image_.isNull()) return QPointF(-1, -1);

    float vw = static_cast<float>(width());
    float vh = static_cast<float>(height());
    float iw = static_cast<float>(image_.width());
    float ih = static_cast<float>(image_.height());
    float imageAspect = iw / ih;
    float viewportAspect = vw / vh;

    float scaleX, scaleY;
    if (imageAspect > viewportAspect) {
        scaleX = zoom_;
        scaleY = (viewportAspect / imageAspect) * zoom_;
    } else {
        scaleX = (imageAspect / viewportAspect) * zoom_;
        scaleY = zoom_;
    }

    // Screen -> NDC: [-1, 1]
    float ndcX = (2.0f * screenPos.x() / vw) - 1.0f;
    float ndcY = 1.0f - (2.0f * screenPos.y() / vh);

    // Undo transform: ndc = transform * quad_pos
    float posNdcX = (ndcX - panX_) / scaleX;
    float posNdcY = (ndcY - panY_) / (-scaleY);

    // NDC [-1,1] -> image [0, width/height]
    float imgX = (posNdcX + 1.0f) * 0.5f * iw;
    float imgY = (1.0f - posNdcY) * 0.5f * ih;

    return QPointF(imgX, imgY);
}

void GpuImageViewer::updatePixelInfo(const QPoint& mousePos)
{
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

void GpuImageViewer::wheelEvent(QWheelEvent* event)
{
    if (image_.isNull()) return;

    QPointF mousePos = event->position();
    QPointF imgBefore = screenToImage(mousePos);

    float factor = (event->angleDelta().y() > 0) ? 1.15f : 1.0f / 1.15f;
    zoom_ = std::clamp(zoom_ * factor, MIN_ZOOM, MAX_ZOOM);

    // Zoom toward cursor: adjust pan so image point under cursor stays fixed
    QPointF imgAfter = screenToImage(mousePos);

    // Convert the image-space delta back to NDC delta
    float iw = static_cast<float>(image_.width());
    float ih = static_cast<float>(image_.height());
    float vw = static_cast<float>(width());
    float vh = static_cast<float>(height());
    float imageAspect = iw / ih;
    float viewportAspect = vw / vh;

    float scaleX, scaleY;
    if (imageAspect > viewportAspect) {
        scaleX = zoom_;
        scaleY = (viewportAspect / imageAspect) * zoom_;
    } else {
        scaleX = (imageAspect / viewportAspect) * zoom_;
        scaleY = zoom_;
    }

    // image delta -> NDC delta: (delta_px / image_size) * 2 * scale
    float dxNdc = (imgAfter.x() - imgBefore.x()) / iw * 2.0f * scaleX;
    float dyNdc = (imgAfter.y() - imgBefore.y()) / ih * 2.0f * scaleY;
    panX_ += dxNdc;
    panY_ -= dyNdc;

    clampPan();
    update();
    updatePixelInfo(mousePos.toPoint());
}

void GpuImageViewer::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        lastDragPos_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void GpuImageViewer::mouseMoveEvent(QMouseEvent* event)
{
    if (dragging_) {
        QPointF delta = event->pos() - lastDragPos_;
        lastDragPos_ = event->pos();

        float vw = static_cast<float>(width());
        float vh = static_cast<float>(height());
        // Convert pixel delta to NDC delta
        panX_ += 2.0f * delta.x() / vw;
        panY_ -= 2.0f * delta.y() / vh;
        clampPan();
        update();
    }
    updatePixelInfo(event->pos());
}

void GpuImageViewer::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        dragging_ = false;
        setCursor(Qt::ArrowCursor);
    }
}

void GpuImageViewer::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        resetView();
    }
}
