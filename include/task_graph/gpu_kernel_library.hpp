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
// kernel_name        - kernel 入口名（Metal function / GLSL 入口）
// kernel_source      - Metal kernel 源码（含 #include <metal_stdlib>）
// kernel_source_glsl - Vulkan compute 的 GLSL 源码（空表示不支持 Vulkan）
// params             - 参数契约（供 UI / 工具链发现）
//
// 回调签名中 input 为上游输入 Image，params 为 task 配置参数。
struct GpuImageOp {
    std::string kernel_name;
    std::string kernel_source;
    std::string kernel_source_glsl;
    std::vector<ParamSpec> params;

    // 输入端口数：1 = 单输入（buffer(0)=in, buffer(1)=dst）；
    // 2 = 双输入（buffer(0)=in, buffer(1)=in2, buffer(2)=dst），
    // uniform 绑定在 bindings 之后（Metal buffer(N) / Vulkan push_constant）。
    int input_count{1};

    // 期望的输入通道数（3 = RGB，4 = RGBA）；UINT8 类型始终必需。
    int input_channels{3};
    int input2_channels{3};

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
// 插件通过 register_op 注册自带 kernel 的算子；卸载时必须 unregister_op，
// 否则 ops_ 中的 std::function（invoke thunk 位于插件 SO 内）成为悬垂指针。
class GpuKernelLibrary {
public:
    static GpuKernelLibrary& instance();

    void register_op(const std::string& op_name, GpuImageOp op);
    void unregister_op(const std::string& op_name);
    const GpuImageOp* find(const std::string& op_name) const;
    std::vector<std::string> available_ops() const;

private:
    GpuKernelLibrary();
    void register_builtin_ops();

    std::unordered_map<std::string, GpuImageOp> ops_;
    mutable std::mutex mutex_;
};

}  // namespace task_graph
