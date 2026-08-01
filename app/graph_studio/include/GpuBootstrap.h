#ifndef GRAPH_STUDIO_GPU_BOOTSTRAP_H
#define GRAPH_STUDIO_GPU_BOOTSTRAP_H

namespace graph_studio {

// 启动期初始化 GPU backend（平台相关：macOS->Metal, Windows/Linux->Vulkan,
// WASM/无可用 backend -> 跳过）。fail-open：init 失败仅记 WARN，不阻断 GUI
// 启动，非 GPU 节点仍可执行；GPU 节点运行时会因 backend 不可用而 FAILED。
void InitGpuBackend();

// 进程退出前释放 GPU backend 资源。与 InitGpuBackend() 配对。
void ShutdownGpuBackend();

}  // namespace graph_studio

#endif  // GRAPH_STUDIO_GPU_BOOTSTRAP_H
