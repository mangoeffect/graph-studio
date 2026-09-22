#ifndef IMAGE_RESULT_STORE_H
#define IMAGE_RESULT_STORE_H

// ImageResultStore：执行后图像结果的采集与懒转换（应用侧）。
//
// 职责（自 GraphViewModel 迁出）：
//  - 采集阶段只做类型探测（Image/cv::Mat），原始 std::any 原样保存——
//    GPU 驻留（纹理）输出不在此回 CPU；
//  - imageResult() 首次取某 key 时才 ensure_cpu 下载并转 QImage
//    （cleanup function 零拷贝共享源像素），转换结果缓存避免重复下载。
// key 格式："nodeId:port"（单输出节点端口名为 "out"）。

#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <any>
#include <unordered_map>

namespace task_graph {
struct TaskResult;
}

namespace graph_studio {

class ImageResultStore {
public:
    // 从一轮执行结果采集图像输出（多输出按端口，单输出按 "out"）。
    // 采集前清空上一轮（单槽语义，循环模式下不随轮数积累）。
    void collectFrom(const std::unordered_map<std::string, task_graph::TaskResult>& results);

    QStringList keys() const;
    QImage image(const QString& key) const;  // 懒转换 + 缓存
    void clear();
    bool isEmpty() const { return imageResults_.isEmpty(); }

private:
    QHash<QString, std::any> imageResults_;
    mutable QHash<QString, QImage> imageResultCache_;
};

} // namespace graph_studio

#endif // IMAGE_RESULT_STORE_H
