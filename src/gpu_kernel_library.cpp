#include <task_graph/gpu_kernel_library.hpp>
#include <cstring>

namespace task_graph {

namespace {

// ---- uniform 打包 helper ----

std::vector<uint8_t> pack_u32(std::initializer_list<uint32_t> vals) {
    std::vector<uint8_t> data(vals.size() * sizeof(uint32_t));
    std::memcpy(data.data(), vals.begin(), data.size());
    return data;
}

uint32_t float_to_u32(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}

// ---- Metal kernel 源码 ----

const char* kBoxBlurSource = R"METAL(
#include <metal_stdlib>
using namespace metal;

kernel void box_blur(device const uchar* src [[buffer(0)]],
                     device uchar* dst       [[buffer(1)]],
                     constant const uint* u  [[buffer(2)]],
                     uint2 gid [[thread_position_in_grid]]) {
    uint width = u[0], height = u[1], radius = u[2];
    if (gid.x >= width || gid.y >= height) return;
    uint3 sum = 0;
    uint count = 0;
    for (int dy = -int(radius); dy <= int(radius); dy++) {
        for (int dx = -int(radius); dx <= int(radius); dx++) {
            int x = int(gid.x) + dx;
            int y = int(gid.y) + dy;
            if (x >= 0 && x < int(width) && y >= 0 && y < int(height)) {
                uint idx = (uint(y) * width + uint(x)) * 3;
                sum += uint3(src[idx], src[idx+1], src[idx+2]);
                count++;
            }
        }
    }
    uint idx = (gid.y * width + gid.x) * 3;
    dst[idx]   = uchar(sum.r / count);
    dst[idx+1] = uchar(sum.g / count);
    dst[idx+2] = uchar(sum.b / count);
}
)METAL";

const char* kGaussianBlurSource = R"METAL(
#include <metal_stdlib>
using namespace metal;

kernel void gaussian_blur(device const uchar* src [[buffer(0)]],
                          device uchar* dst       [[buffer(1)]],
                          constant const uint* u  [[buffer(2)]],
                          uint2 gid [[thread_position_in_grid]]) {
    uint width = u[0], height = u[1], radius = u[2];
    float sigma = as_type<float>(u[3]);
    if (sigma < 0.001) sigma = 0.001;
    if (gid.x >= width || gid.y >= height) return;
    float3 sum = 0.0;
    float weight_sum = 0.0;
    float s2 = 2.0 * sigma * sigma;
    for (int dy = -int(radius); dy <= int(radius); dy++) {
        for (int dx = -int(radius); dx <= int(radius); dx++) {
            int x = int(gid.x) + dx;
            int y = int(gid.y) + dy;
            if (x >= 0 && x < int(width) && y >= 0 && y < int(height)) {
                float w = exp(-(float(dx*dx + dy*dy)) / s2);
                uint idx = (uint(y) * width + uint(x)) * 3;
                sum += float3(float(src[idx]), float(src[idx+1]), float(src[idx+2])) * w;
                weight_sum += w;
            }
        }
    }
    sum /= weight_sum;
    uint idx = (gid.y * width + gid.x) * 3;
    dst[idx]   = uchar(clamp(sum.r, 0.0, 255.0));
    dst[idx+1] = uchar(clamp(sum.g, 0.0, 255.0));
    dst[idx+2] = uchar(clamp(sum.b, 0.0, 255.0));
}
)METAL";

const char* kGrayscaleSource = R"METAL(
#include <metal_stdlib>
using namespace metal;

kernel void rgb_to_gray(device const uchar* src [[buffer(0)]],
                        device uchar* dst       [[buffer(1)]],
                        constant const uint* u  [[buffer(2)]],
                        uint2 gid [[thread_position_in_grid]]) {
    uint width = u[0], height = u[1];
    if (gid.x >= width || gid.y >= height) return;
    uint idx = (gid.y * width + gid.x) * 3;
    float gray = 0.299 * float(src[idx]) + 0.587 * float(src[idx+1]) + 0.114 * float(src[idx+2]);
    dst[gid.y * width + gid.x] = uchar(clamp(gray, 0.0, 255.0));
}
)METAL";

const char* kBrightnessContrastSource = R"METAL(
#include <metal_stdlib>
using namespace metal;

kernel void brightness_contrast(device const uchar* src [[buffer(0)]],
                                device uchar* dst       [[buffer(1)]],
                                constant const uint* u  [[buffer(2)]],
                                uint2 gid [[thread_position_in_grid]]) {
    uint width = u[0], height = u[1];
    float brightness = as_type<float>(u[2]);
    float contrast = as_type<float>(u[3]);
    if (gid.x >= width || gid.y >= height) return;
    uint idx = (gid.y * width + gid.x) * 3;
    for (int c = 0; c < 3; c++) {
        float val = float(src[idx + c]);
        val = (val - 128.0) * (1.0 + contrast) + 128.0 + brightness * 255.0;
        dst[idx + c] = uchar(clamp(val, 0.0, 255.0));
    }
}
)METAL";

const char* kResizeSource = R"METAL(
#include <metal_stdlib>
using namespace metal;

kernel void bilinear_resize(device const uchar* src [[buffer(0)]],
                            device uchar* dst       [[buffer(1)]],
                            constant const uint* u  [[buffer(2)]],
                            uint2 gid [[thread_position_in_grid]]) {
    uint src_w = u[0], src_h = u[1], dst_w = u[2], dst_h = u[3];
    if (gid.x >= dst_w || gid.y >= dst_h) return;
    float x_ratio = (float(gid.x) + 0.5) / float(dst_w) * float(src_w) - 0.5;
    float y_ratio = (float(gid.y) + 0.5) / float(dst_h) * float(src_h) - 0.5;
    uint x0 = uint(clamp(x_ratio, 0.0, float(src_w - 1)));
    uint y0 = uint(clamp(y_ratio, 0.0, float(src_h - 1)));
    uint x1 = min(x0 + 1, src_w - 1);
    uint y1 = min(y0 + 1, src_h - 1);
    float fx = clamp(x_ratio - float(x0), 0.0, 1.0);
    float fy = clamp(y_ratio - float(y0), 0.0, 1.0);
    for (int c = 0; c < 3; c++) {
        float top = float(src[(y0 * src_w + x0) * 3 + c]) * (1.0 - fx) +
                    float(src[(y0 * src_w + x1) * 3 + c]) * fx;
        float bot = float(src[(y1 * src_w + x0) * 3 + c]) * (1.0 - fx) +
                    float(src[(y1 * src_w + x1) * 3 + c]) * fx;
        float val = top * (1.0 - fy) + bot * fy;
        dst[(gid.y * dst_w + gid.x) * 3 + c] = uchar(clamp(val, 0.0, 255.0));
    }
}
)METAL";

// ---- Vulkan GLSL kernel 源码（与上方 MSL 逐算子等价）----
// 约定：binding 0 = 输入 src，binding 1 = 输出 dst；uniform 走 push_constant
//（uint u[16]，float 参数按位打包），local_size 固定 8x8（与后端 dispatch 换算一致）。

const char* kGlslHeader = R"GLSL(#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, set = 0, binding = 0) readonly buffer SrcBuf { uint8_t src[]; };
layout(std430, set = 0, binding = 1) buffer DstBuf { uint8_t dst[]; };
layout(push_constant) uniform Uni { uint u[16]; } pc;
)GLSL";

const char* kBoxBlurGlsl = R"GLSL(
void main() {
    uint width = pc.u[0], height = pc.u[1], radius = pc.u[2];
    uvec2 gid = gl_GlobalInvocationID.xy;
    if (gid.x >= width || gid.y >= height) return;
    uvec3 sum = uvec3(0);
    uint count = 0;
    for (int dy = -int(radius); dy <= int(radius); dy++) {
        for (int dx = -int(radius); dx <= int(radius); dx++) {
            int x = int(gid.x) + dx;
            int y = int(gid.y) + dy;
            if (x >= 0 && x < int(width) && y >= 0 && y < int(height)) {
                uint idx = (uint(y) * width + uint(x)) * 3;
                sum += uvec3(uint(src[idx]), uint(src[idx+1]), uint(src[idx+2]));
                count++;
            }
        }
    }
    uint idx = (gid.y * width + gid.x) * 3;
    dst[idx]   = uint8_t(sum.r / count);
    dst[idx+1] = uint8_t(sum.g / count);
    dst[idx+2] = uint8_t(sum.b / count);
}
)GLSL";

const char* kGaussianBlurGlsl = R"GLSL(
void main() {
    uint width = pc.u[0], height = pc.u[1], radius = pc.u[2];
    float sigma = uintBitsToFloat(pc.u[3]);
    if (sigma < 0.001) sigma = 0.001;
    uvec2 gid = gl_GlobalInvocationID.xy;
    if (gid.x >= width || gid.y >= height) return;
    vec3 sum = vec3(0.0);
    float weight_sum = 0.0;
    float s2 = 2.0 * sigma * sigma;
    for (int dy = -int(radius); dy <= int(radius); dy++) {
        for (int dx = -int(radius); dx <= int(radius); dx++) {
            int x = int(gid.x) + dx;
            int y = int(gid.y) + dy;
            if (x >= 0 && x < int(width) && y >= 0 && y < int(height)) {
                float w = exp(-float(dx*dx + dy*dy) / s2);
                uint idx = (uint(y) * width + uint(x)) * 3;
                sum += vec3(float(src[idx]), float(src[idx+1]), float(src[idx+2])) * w;
                weight_sum += w;
            }
        }
    }
    sum /= weight_sum;
    uint idx = (gid.y * width + gid.x) * 3;
    dst[idx]   = uint8_t(clamp(sum.r, 0.0, 255.0));
    dst[idx+1] = uint8_t(clamp(sum.g, 0.0, 255.0));
    dst[idx+2] = uint8_t(clamp(sum.b, 0.0, 255.0));
}
)GLSL";

const char* kGrayscaleGlsl = R"GLSL(
void main() {
    uint width = pc.u[0], height = pc.u[1];
    uvec2 gid = gl_GlobalInvocationID.xy;
    if (gid.x >= width || gid.y >= height) return;
    uint idx = (gid.y * width + gid.x) * 3;
    float gray = 0.299 * float(src[idx]) + 0.587 * float(src[idx+1]) + 0.114 * float(src[idx+2]);
    dst[gid.y * width + gid.x] = uint8_t(clamp(gray, 0.0, 255.0));
}
)GLSL";

const char* kBrightnessContrastGlsl = R"GLSL(
void main() {
    uint width = pc.u[0], height = pc.u[1];
    float brightness = uintBitsToFloat(pc.u[2]);
    float contrast = uintBitsToFloat(pc.u[3]);
    uvec2 gid = gl_GlobalInvocationID.xy;
    if (gid.x >= width || gid.y >= height) return;
    uint idx = (gid.y * width + gid.x) * 3;
    for (int c = 0; c < 3; c++) {
        float val = float(src[idx + c]);
        val = (val - 128.0) * (1.0 + contrast) + 128.0 + brightness * 255.0;
        dst[idx + c] = uint8_t(clamp(val, 0.0, 255.0));
    }
}
)GLSL";

const char* kResizeGlsl = R"GLSL(
void main() {
    uint src_w = pc.u[0], src_h = pc.u[1], dst_w = pc.u[2], dst_h = pc.u[3];
    uvec2 gid = gl_GlobalInvocationID.xy;
    if (gid.x >= dst_w || gid.y >= dst_h) return;
    float x_ratio = (float(gid.x) + 0.5) / float(dst_w) * float(src_w) - 0.5;
    float y_ratio = (float(gid.y) + 0.5) / float(dst_h) * float(src_h) - 0.5;
    uint x0 = uint(clamp(x_ratio, 0.0, float(src_w - 1)));
    uint y0 = uint(clamp(y_ratio, 0.0, float(src_h - 1)));
    uint x1 = min(x0 + 1, src_w - 1);
    uint y1 = min(y0 + 1, src_h - 1);
    float fx = clamp(x_ratio - float(x0), 0.0, 1.0);
    float fy = clamp(y_ratio - float(y0), 0.0, 1.0);
    for (int c = 0; c < 3; c++) {
        float top = float(src[(y0 * src_w + x0) * 3 + c]) * (1.0 - fx) +
                    float(src[(y0 * src_w + x1) * 3 + c]) * fx;
        float bot = float(src[(y1 * src_w + x0) * 3 + c]) * (1.0 - fx) +
                    float(src[(y1 * src_w + x1) * 3 + c]) * fx;
        float val = top * (1.0 - fy) + bot * fy;
        dst[(gid.y * dst_w + gid.x) * 3 + c] = uint8_t(clamp(val, 0.0, 255.0));
    }
}
)GLSL";

// 拼接 header + main body 成完整 GLSL 源（kernel_body 不含 #version 等全局声明）
std::string glsl_source(const char* kernel_body) {
    return std::string(kGlslHeader) + kernel_body;
}

}  // namespace

// ====================== GpuKernelLibrary ======================

GpuKernelLibrary& GpuKernelLibrary::instance() {
    static GpuKernelLibrary lib;
    return lib;
}

GpuKernelLibrary::GpuKernelLibrary() {
    register_builtin_ops();
}

void GpuKernelLibrary::register_op(const std::string& op_name, GpuImageOp op) {
    std::lock_guard<std::mutex> lock(mutex_);
    ops_[op_name] = std::move(op);
}

void GpuKernelLibrary::unregister_op(const std::string& op_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    ops_.erase(op_name);
}

const GpuImageOp* GpuKernelLibrary::find(const std::string& op_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ops_.find(op_name);
    if (it == ops_.end()) return nullptr;
    return &it->second;
}

std::vector<std::string> GpuKernelLibrary::available_ops() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(ops_.size());
    for (const auto& [name, _] : ops_) {
        names.push_back(name);
    }
    return names;
}

void GpuKernelLibrary::register_builtin_ops() {
    // ---- gpu_box_blur ----
    {
        GpuImageOp op;
        op.kernel_name = "box_blur";
        op.kernel_source = kBoxBlurSource;
        op.kernel_source_glsl = glsl_source(kBoxBlurGlsl);
        op.params = {make_int_param("kernel_size", 5, 1, 99, 2)};
        op.compute_grid = [](const Image& in, const TaskParams&,
                             uint32_t& gx, uint32_t& gy, uint32_t& gz) {
            gx = static_cast<uint32_t>(in.width);
            gy = static_cast<uint32_t>(in.height);
            gz = 1;
        };
        op.pack_uniforms = [](const Image& in, const TaskParams& p) {
            int ksize = p.get_int("kernel_size").value_or(5);
            if (ksize % 2 == 0) ksize++;
            if (ksize < 1) ksize = 1;
            return pack_u32({(uint32_t)in.width, (uint32_t)in.height,
                             (uint32_t)((ksize - 1) / 2)});
        };
        op.compute_output_size = [](const Image& in, const TaskParams&,
                                     int& w, int& h, int& c) {
            w = in.width; h = in.height; c = in.channels;
        };
        ops_["gpu_box_blur"] = std::move(op);
    }

    // ---- gpu_gaussian_blur ----
    {
        GpuImageOp op;
        op.kernel_name = "gaussian_blur";
        op.kernel_source = kGaussianBlurSource;
        op.kernel_source_glsl = glsl_source(kGaussianBlurGlsl);
        op.params = {
            make_int_param("kernel_size", 5, 1, 99, 2),
            make_float_param("sigma", 0.0f, 0.0, 100.0)
        };
        op.compute_grid = [](const Image& in, const TaskParams&,
                             uint32_t& gx, uint32_t& gy, uint32_t& gz) {
            gx = static_cast<uint32_t>(in.width);
            gy = static_cast<uint32_t>(in.height);
            gz = 1;
        };
        op.pack_uniforms = [](const Image& in, const TaskParams& p) {
            int ksize = p.get_int("kernel_size").value_or(5);
            if (ksize % 2 == 0) ksize++;
            if (ksize < 1) ksize = 1;
            float sigma = p.get_float("sigma").value_or(0.0f);
            if (sigma <= 0.0f) sigma = static_cast<float>(ksize - 1) / 6.0f;
            return pack_u32({(uint32_t)in.width, (uint32_t)in.height,
                             (uint32_t)((ksize - 1) / 2), float_to_u32(sigma)});
        };
        op.compute_output_size = [](const Image& in, const TaskParams&,
                                     int& w, int& h, int& c) {
            w = in.width; h = in.height; c = in.channels;
        };
        ops_["gpu_gaussian_blur"] = std::move(op);
    }

    // ---- gpu_grayscale ----
    {
        GpuImageOp op;
        op.kernel_name = "rgb_to_gray";
        op.kernel_source = kGrayscaleSource;
        op.kernel_source_glsl = glsl_source(kGrayscaleGlsl);
        op.params = {};
        op.compute_grid = [](const Image& in, const TaskParams&,
                             uint32_t& gx, uint32_t& gy, uint32_t& gz) {
            gx = static_cast<uint32_t>(in.width);
            gy = static_cast<uint32_t>(in.height);
            gz = 1;
        };
        op.pack_uniforms = [](const Image& in, const TaskParams&) {
            return pack_u32({(uint32_t)in.width, (uint32_t)in.height});
        };
        op.compute_output_size = [](const Image& in, const TaskParams&,
                                     int& w, int& h, int& c) {
            w = in.width; h = in.height; c = 1;
        };
        ops_["gpu_grayscale"] = std::move(op);
    }

    // ---- gpu_brightness_contrast ----
    {
        GpuImageOp op;
        op.kernel_name = "brightness_contrast";
        op.kernel_source = kBrightnessContrastSource;
        op.kernel_source_glsl = glsl_source(kBrightnessContrastGlsl);
        op.params = {
            make_float_param("brightness", 0.0f, -1.0, 1.0),
            make_float_param("contrast", 0.0f, -1.0, 1.0)
        };
        op.compute_grid = [](const Image& in, const TaskParams&,
                             uint32_t& gx, uint32_t& gy, uint32_t& gz) {
            gx = static_cast<uint32_t>(in.width);
            gy = static_cast<uint32_t>(in.height);
            gz = 1;
        };
        op.pack_uniforms = [](const Image& in, const TaskParams& p) {
            float brightness = p.get_float("brightness").value_or(0.0f);
            float contrast = p.get_float("contrast").value_or(0.0f);
            return pack_u32({(uint32_t)in.width, (uint32_t)in.height,
                             float_to_u32(brightness), float_to_u32(contrast)});
        };
        op.compute_output_size = [](const Image& in, const TaskParams&,
                                     int& w, int& h, int& c) {
            w = in.width; h = in.height; c = in.channels;
        };
        ops_["gpu_brightness_contrast"] = std::move(op);
    }

    // ---- gpu_resize ----
    {
        GpuImageOp op;
        op.kernel_name = "bilinear_resize";
        op.kernel_source = kResizeSource;
        op.kernel_source_glsl = glsl_source(kResizeGlsl);
        op.params = {
            make_int_param("target_w", 256, 1, 16384),
            make_int_param("target_h", 256, 1, 16384)
        };
        op.compute_grid = [](const Image& in, const TaskParams& p,
                             uint32_t& gx, uint32_t& gy, uint32_t& gz) {
            int dw = p.get_int("target_w").value_or(in.width);
            int dh = p.get_int("target_h").value_or(in.height);
            gx = static_cast<uint32_t>(dw);
            gy = static_cast<uint32_t>(dh);
            gz = 1;
        };
        op.pack_uniforms = [](const Image& in, const TaskParams& p) {
            int dw = p.get_int("target_w").value_or(in.width);
            int dh = p.get_int("target_h").value_or(in.height);
            return pack_u32({(uint32_t)in.width, (uint32_t)in.height,
                             (uint32_t)dw, (uint32_t)dh});
        };
        op.compute_output_size = [](const Image& in, const TaskParams& p,
                                     int& w, int& h, int& c) {
            w = p.get_int("target_w").value_or(in.width);
            h = p.get_int("target_h").value_or(in.height);
            c = in.channels;
        };
        ops_["gpu_resize"] = std::move(op);
    }
}

}  // namespace task_graph
