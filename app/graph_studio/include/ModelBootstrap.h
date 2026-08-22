#ifndef GRAPH_STUDIO_MODEL_BOOTSTRAP_H
#define GRAPH_STUDIO_MODEL_BOOTSTRAP_H

namespace graph_studio {

// 启动期安装全局 ModelFinder（task_graph::set_model_finder）：任务参数里的
// 模型名（如 "face_landmarker.task"）在此解析为实际文件路径。查找目录候选：
//   1) 环境变量 GRAPH_STUDIO_MODELS_DIR（dev 脚本注入 / Linux AppRun 注入）
//   2) <exe 目录>/models（Windows：MSIX 安装布局，exe 与 models/ 同级）
//   3) <exe 目录>/../Resources/models（macOS .app bundle）
// 文件名依次尝试 原名 / 原名.task / 原名.tflite。全未命中返回空串，任务回
// 退到图目录相对路径解析 —— fail-open，无 models 目录时行为与未装 finder 一致。
// 需在 QApplication 构造之后调用（依赖 applicationDirPath）。
void InitModelFinder();

// 进程退出前注销 ModelFinder。与 InitModelFinder() 配对。
void ShutdownModelFinder();

}  // namespace graph_studio

#endif  // GRAPH_STUDIO_MODEL_BOOTSTRAP_H
