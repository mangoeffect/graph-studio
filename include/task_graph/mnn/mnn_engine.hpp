#pragma once

// MNN（Alibaba）推理引擎 —— 主框架公共引擎 API。
//
// 供后续待实现的具体子模块直接调用（图像 → 预处理 → 推理 → 全部输出张量），
// 无需自行绑定 MNN C++ API。真实 MNN 头文件只出现在 src/mnn/ 下（pimpl），
// 对 SDK 消费者零依赖。
//
// 编译期三态（CMake TASK_GRAPH_ENABLE_MNN）：
//   OFF                    -> 本文件不参与编译
//   ON、找到预编译库       -> 定义 TASK_GRAPH_MNN_AVAILABLE，真实推理
//   ON、未找到库           -> stub：create() 恒失败，任务 execute 上报可读错误
//
// 线程约束：同一实例不可并发调用（MNN Interpreter/Session 无跨线程官方保证，
// 上游 issue #592/#2419）；DAG 并行分支请各持独立实例。

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <task_graph/data_types.hpp>
#include <task_graph/mnn/mnn_types.hpp>

namespace task_graph {

struct MnnEngineOptions {
    std::string model_path;
    std::string device{"auto"};        // "cpu" | "metal" | "auto"
    int threads{4};
    std::string precision{"normal"};   // "normal" | "high" | "low"
    int input_width{0};                // 0 = 沿用模型输入 shape
    int input_height{0};
    float mean[3]{0.f, 0.f, 0.f};
    float scale[3]{1.f, 1.f, 1.f};     // 归一化除数（MNN normal 项 = 1/scale）
    std::string source_format{"bgr"};  // "bgr" | "rgb" | "rgba" | "gray"
};

class MnnEngine {
public:
    // 创建失败返回 nullptr 并填充 err（模型不存在/设备后端未编入等）。
    static std::shared_ptr<MnnEngine> create(const MnnEngineOptions& opts,
                                             std::string& err);
    ~MnnEngine();

    // 模型（或 resize 后）输入的逻辑 shape（N,C,H,W 序）。
    const std::vector<int>& input_shape() const;

    // 单帧推理：uint8 图像 → 预处理（缩放/格式转换/mean/std 归一化）→ 推理
    // → 全部输出张量拷回 host。
    bool run(const uint8_t* data, int width, int height, MnnSourceFormat format,
             std::vector<MnnTensorData>& outputs, std::string& err);

    // 便捷重载：直接吃框架 Image（内部 ensure_cpu 后按 pixel_format 推导
    // source_format）。
    bool run(const Image& image, std::vector<MnnTensorData>& outputs,
             std::string& err);

private:
    MnnEngine();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace task_graph
