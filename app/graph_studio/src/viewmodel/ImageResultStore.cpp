#include "viewmodel/ImageResultStore.h"

#include <task_graph_api.hpp>
#include <opencv2/opencv.hpp>

using namespace graph_studio;

namespace {

// ---- 零拷贝图像转换：cv::Mat / task_graph::Image -> QImage ----
// QImage 通过 cleanup function 持有源数据的引用计数，直接共享像素缓冲，
// 析构时回调释放源对象，避免 QImage::copy() 二次拷贝。

void matCleanup(void* info) {
    delete static_cast<cv::Mat*>(info);
}

QImage matToQImage(const cv::Mat& src) {
    if (src.empty()) return {};
    cv::Mat mat = src;  // refcount++，共享像素缓冲
    QImage::Format fmt = QImage::Format_Invalid;
    switch (mat.channels()) {
        case 1:  fmt = QImage::Format_Grayscale8; break;   // 灰度(Sobel/Laplacian)
        case 3:  fmt = QImage::Format_BGR888;      break;  // BGR 直接映射，零拷贝
        case 4: {                                          // BGRA：Qt 无 BGRA8888 格式
            cv::Mat t;
            cv::cvtColor(mat, t, cv::COLOR_BGRA2RGBA);
            mat = t;
            fmt = QImage::Format_RGBA8888;
            break;
        }
        default: return {};
    }
    // keep 持 Mat 引用计数；QImage 析构时 delete keep，refcount 归零才释放像素
    auto* keep = new cv::Mat(mat);
    return QImage(keep->data, keep->cols, keep->rows, keep->step,
                  fmt, matCleanup, keep);
}

void imageDataCleanup(void* info) {
    delete static_cast<std::shared_ptr<std::vector<uint8_t>>*>(info);
}

QImage imageToQImage(const task_graph::Image& src) {
    // 拷贝 Image 结构体（浅拷贝，共享 data 的 shared_ptr）；ensure_cpu 可能改状态，隔离之
    task_graph::Image img = src;
    if (!img.ensure_cpu() || !img.data || img.data->empty()) return {};
    QImage::Format fmt = QImage::Format_Invalid;
    switch (img.channels) {
        case 1: fmt = QImage::Format_Grayscale8; break;
        case 3: fmt = (img.pixel_format == task_graph::PixelFormat::BGR)
                          ? QImage::Format_BGR888 : QImage::Format_RGB888; break;
        case 4: fmt = QImage::Format_RGBA8888; break;
        default: return {};
    }
    // keep 持 shared_ptr<vector<uint8_t>> 引用计数，QImage 析构时释放
    auto* keep = new std::shared_ptr<std::vector<uint8_t>>(img.data);
    return QImage(keep->get()->data(), img.width, img.height,
                  img.width * img.channels, fmt, imageDataCleanup, keep);
}

// 从 std::any 提取图像转 QImage。type-check-first：WASM -fno-exceptions 下
// any_cast 失败会 abort，必须先用 type() 比对。GPU 驻留 Image 在此触发
// ensure_cpu 下载——仅由 imageResult()（用户选中显示）按需调用。
std::optional<QImage> anyToQImage(const std::any& v) {
    if (!v.has_value()) return std::nullopt;
    if (v.type() == typeid(cv::Mat)) {
        return matToQImage(std::any_cast<cv::Mat>(v));
    }
    if (v.type() == typeid(task_graph::Image)) {
        return imageToQImage(std::any_cast<task_graph::Image>(v));
    }
    return std::nullopt;
}

// 结果采集阶段的类型探测（不转换、不触发 GPU→CPU 同步）。
bool isImageAny(const std::any& v) {
    return v.has_value() && (v.type() == typeid(task_graph::Image) ||
                             v.type() == typeid(cv::Mat));
}

}  // namespace

namespace graph_studio {

void ImageResultStore::collectFrom(
    const std::unordered_map<std::string, task_graph::TaskResult>& results) {
    clear();
    for (auto& [id, result] : results) {
        if (!result.is_success()) continue;
        const QString qid = QString::fromStdString(id);
        if (!result.outputs.empty()) {
            for (auto& [port, anyVal] : result.outputs) {
                if (isImageAny(anyVal)) {
                    QString key = qid + ":" + QString::fromStdString(port);
                    imageResults_[key] = std::move(anyVal);
                }
            }
        } else if (isImageAny(result.value)) {
            imageResults_[qid + ":out"] = std::move(result.value);
        }
    }
}

QStringList ImageResultStore::keys() const {
    return imageResults_.keys();
}

QImage ImageResultStore::image(const QString& key) const {
    // 已转换过直接返回缓存（GPU 驻留结果只下载一次）
    auto cached = imageResultCache_.constFind(key);
    if (cached != imageResultCache_.constEnd()) return cached.value();

    auto it = imageResults_.constFind(key);
    if (it == imageResults_.constEnd()) return QImage();

    QImage img;
    if (auto converted = anyToQImage(it.value())) {
        img = std::move(*converted);
    }
    if (!img.isNull()) {
        imageResultCache_.insert(key, img);
    }
    return img;
}

void ImageResultStore::clear() {
    imageResults_.clear();
    imageResultCache_.clear();
}

} // namespace graph_studio
