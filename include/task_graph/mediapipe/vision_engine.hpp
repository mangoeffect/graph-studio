#pragma once

// MediaPipe Vision 引擎 —— 主框架公共引擎 API。
//
// 供后续待实现的具体子模块直接调用（创建任务句柄 → 单帧图像推理 →
// VisionResult），无需自行绑定 MediaPipe C API（Mp*）。真实 Mp 头文件只出现
// 在 src/mediapipe/ 下（pimpl），对 SDK 消费者零依赖。
//
// 编译期三态（CMake TASK_GRAPH_ENABLE_MEDIAPIPE）：
//   OFF                    -> 本文件不参与编译
//   ON、找到预编译库       -> 定义 TASK_GRAPH_MEDIAPIPE_AVAILABLE，真实推理
//   ON、未找到库           -> stub：create() 恒失败（可读错误），任务 execute
//                             返回 FAILED，图仍可反序列化/编辑
//
// 运行模式：当前仅 IMAGE（单帧）；VIDEO/LIVE_STREAM 为后续路线。
// 线程约束：同一实例不可并发调用（句柄内是 MediaPipe graph，非线程安全）；
// DAG 并行分支请各持独立实例。
//
// WASM/移动端 -fno-exceptions 约定：API 只以 err 出参报错，绝不抛出。

#include <memory>
#include <string>

#include <task_graph/data_types.hpp>
#include <task_graph/mediapipe/vision_types.hpp>

namespace task_graph {

enum class MpVisionTask {
    FaceLandmarker,
    HandLandmarker,
    PoseLandmarker,
    ObjectDetector,
    FaceDetector,
    ImageClassifier,
    ImageEmbedder,
    ImageSegmenter,
    GestureRecognizer,
    HolisticLandmarker,
};

// 全任务共用选项集（union 风格：无关字段被对应任务忽略）。
struct MpVisionOptions {
    std::string model_path;          // .task/.tflite 模型绝对路径
    int delegate{0};                 // 0 = CPU，1 = GPU

    // Landmarker 类
    int num_results{1};              // num_faces / num_hands / num_poses
    float min_detection_confidence{0.5f};
    float min_presence_confidence{0.5f};
    float min_tracking_confidence{0.5f};
    bool output_face_blendshapes{false};
    bool output_transformation_matrixes{false};
    bool output_segmentation_masks{false};

    // Detector / Classifier 类
    int max_results{-1};
    float score_threshold{0.0f};
    float min_suppression_threshold{0.5f};  // FaceDetector NMS

    // Segmenter 类
    bool output_confidence_masks{true};
    bool output_category_mask{false};

    // Embedder 类
    bool l2_normalize{false};
};

class MpVisionEngine {
public:
    // 创建失败返回 nullptr 并填充 err（模型不存在 / stub / GPU 不可用等）。
    static std::shared_ptr<MpVisionEngine> create(MpVisionTask task,
                                                  const MpVisionOptions& opts,
                                                  std::string& err);
    ~MpVisionEngine();

    // 预构建库是否带 GPU delegate（build_mediapipe.py 桌面产物固定
    // --define=MEDIAPIPE_DISABLE_GPU=1 —— 桌面 GPU 官方仅支持 Linux；
    // CPU-only 构建上请求 GPU 不会报错而是挂死 graph 创建，调用方须先拦截）。
    static bool gpu_delegate_available();

    // 单帧推理：uint8 图像（data 生命期覆盖本次调用）→ VisionResult。
    bool run(const uint8_t* data, int width, int height, int channels,
             VisionResult& out, std::string& err);

    // 便捷重载：框架 Image（内部 ensure_cpu；1/3/4 通道 → 对应 MpImageFormat）。
    bool run(const Image& image, VisionResult& out, std::string& err);

private:
    MpVisionEngine();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace task_graph
