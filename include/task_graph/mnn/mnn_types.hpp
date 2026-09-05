#pragma once

// MNN 推理任务的端口数据类型。只依赖标准库（不泄漏 MNN 头文件），
// 跨 SO / 静态链接均以 TG_REGISTER_TYPE 的稳定名传输。

#include <string>
#include <vector>

namespace task_graph {

// 输入图像通道顺序。独立于 PixelFormat 枚举（其 RGB/BGR 同值，仅表通道数）。
enum class MnnSourceFormat {
    BGR,
    RGB,
    RGBA,
    GRAY,
};

inline int mnn_source_channels(MnnSourceFormat f) {
    switch (f) {
        case MnnSourceFormat::GRAY: return 1;
        case MnnSourceFormat::RGBA: return 4;
        default:                    return 3;
    }
}

// 单个输出张量（host 侧 float 拷贝；shape 为该张量自身的维度序，
// 与 MNN::Tensor::shape() 一致——NHWC 模型保持 NHWC，不强行转 NCHW）。
struct MnnTensorData {
    std::string name;
    std::vector<int> shape;
    std::vector<float> data;
};

// mnn_inference 的输出：模型全部输出张量。
struct MnnInferenceResult {
    std::vector<MnnTensorData> tensors;

    const MnnTensorData* find(const std::string& tensor_name) const {
        for (const auto& t : tensors) {
            if (t.name == tensor_name) return &t;
        }
        return nullptr;
    }
};

// mnn_image_classifier 的输出：top-k 类别（score 降序）。
struct MnnCategory {
    int index{-1};
    std::string label;
    float score{0.f};
};

struct MnnClassificationResult {
    std::vector<MnnCategory> categories;
};

}  // namespace task_graph
