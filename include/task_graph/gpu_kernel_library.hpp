#pragma once

#include <task_graph/gpu_image_ops.hpp>
#include <plugin_api.hpp>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace task_graph {

// GpuImageOp：一个 GPU 图像算子的完整描述。
//
// kernel_name   - Metal function 入口名
// kernel_source - Metal kernel 源码（含 #include <metal_stdlib>）
// params        - 参数契约（供 UI / 工具链发现）
//
// 回调签名中 input 为上游输入 Image，params 为 task 配置参数。
struct GpuImageOp {
    std::string kernel_name;
    std::string kernel_source;
    std::vector<ParamSpec> params;

    // 按 input 尺寸 + params 计算 dispatch grid（线程组数）
    std::function<void(const Image& input, const TaskParams& params,
                        uint32_t& grid_x, uint32_t& grid_y, uint32_t& grid_z)> compute_grid;

    // 把 params + input 尺寸打包成 uniform byte 数组（绑定到 buffer(2)）
    std::function<std::vector<uint8_t>(const Image& input,
                                        const TaskParams& params)> pack_uniforms;

    // 计算输出 Image 的 width/height/channels（resize 改尺寸，grayscale 改通道）
    std::function<void(const Image& input, const TaskParams& params,
                        int& out_w, int& out_h, int& out_c)> compute_output_size;
};

// GPU 算子注册表：op_name -> GpuImageOp。首次 instance() 时注册内置算子。
class GpuKernelLibrary {
public:
    static GpuKernelLibrary& instance();

    void register_op(const std::string& op_name, GpuImageOp op);
    const GpuImageOp* find(const std::string& op_name) const;
    std::vector<std::string> available_ops() const;

private:
    GpuKernelLibrary();
    void register_builtin_ops();

    std::unordered_map<std::string, GpuImageOp> ops_;
    mutable std::mutex mutex_;
};

}  // namespace task_graph
