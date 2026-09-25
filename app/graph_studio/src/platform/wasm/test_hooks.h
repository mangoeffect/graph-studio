#ifndef GRAPH_STUDIO_TEST_HOOKS_H
#define GRAPH_STUDIO_TEST_HOOKS_H

namespace graph_studio {

class GraphViewModel;
class MainWindow;

// WASM 专用测试桥：把 window.__gsTest 暴露给浏览器端 E2E
// （scripts/run_e2e_wasm.py），提供确定性的状态断言与图加载入口，
// 绕开浏览器文件选择器与 DOM 级拖拽的不可控性。
// 桌面构建为 no-op。
void InstallTestHooks(GraphViewModel& vm, MainWindow& window);

} // namespace graph_studio

#endif // GRAPH_STUDIO_TEST_HOOKS_H
