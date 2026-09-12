#pragma once

// webgpu.h 版本兼容垫片：wgpu-native（新命名）与 emscripten 旧头（老命名）
// 共用一套后端代码。原生构建直接用 wgpu-native v29 的新名；emsdk < 3.1.4x
// 的 webgpu.h 还是 breaking-change 前的旧名，这里用宏/别名对齐。
// 升级 emsdk 到携带新 webgpu.h 的版本后，本文件的 __EMSCRIPTEN__ 分支可删。

#include <cstring>
#include <string>

#include <webgpu/webgpu.h>

#if defined(__EMSCRIPTEN__) && defined(WGPUSType_ShaderModuleWGSL)
// ---- 旧 emscripten webgpu.h（<= 3.1.4x 时代）命名对齐 ----
// WGSL 描述：WGPUShaderModuleWGSLDescriptor{code: char*} → 新结构/新 sType
#define TG_WGPU_OLD_WGSL_DESCRIPTOR 1
using WGPUShaderSourceWGSL = WGPUShaderModuleWGSLDescriptor;
// sType 值：旧枚举常量直接复用（值不同无妨，sType 只在本头体系内自洽）
#define WGPUSType_ShaderSourceWGSL_Wrapped WGPUSType_ShaderModuleWGSL
// 拷贝结构旧名
using WGPUTexelCopyBufferInfo = WGPUImageCopyBuffer;
using WGPUTexelCopyTextureInfo = WGPUImageCopyTexture;
#endif

#ifndef TG_WGPU_OLD_WGSL_DESCRIPTOR
#define WGPUSType_ShaderSourceWGSL_Wrapped WGPUSType_ShaderSourceWGSL
#endif

namespace task_graph {
namespace wgpu_compat {

#if defined(TG_WGPU_OLD_WGSL_DESCRIPTOR)
// 旧头：code 是 char*
inline WGPUShaderSourceWGSL make_wgsl_source(const char* data, size_t) {
    WGPUShaderSourceWGSL s{};
    s.chain.next = nullptr;
    s.chain.sType = WGPUSType_ShaderModuleWGSL;
    s.code = data;
    return s;
}
#else
// 新头：code 是 WGPUStringView
inline WGPUShaderSourceWGSL make_wgsl_source(const char* data, size_t len) {
    WGPUShaderSourceWGSL s{};
    s.chain.next = nullptr;
    s.chain.sType = WGPUSType_ShaderSourceWGSL;
    s.code.data = data;
    s.code.length = len;
    return s;
}
#endif

inline WGPUShaderSourceWGSL make_wgsl_source(const std::string& code) {
    return make_wgsl_source(code.c_str(), code.size());
}

inline WGPUStringView str_view(const std::string& s) {
    WGPUStringView v;
    v.data = s.c_str();
    v.length = s.size();
    return v;
}

// 字面量/静态串重载：注意 std::string 临时量的 c_str 会在表达式结束悬垂，
// 固定入口名（vs_main/fs_main/kernel 名）必须走本重载
inline WGPUStringView str_view(const char* s) {
    WGPUStringView v;
    v.data = s;
    v.length = std::char_traits<char>::length(s);
    return v;
}

}  // namespace wgpu_compat
}  // namespace task_graph
