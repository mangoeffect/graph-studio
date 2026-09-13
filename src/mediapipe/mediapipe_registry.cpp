#include <task_graph/mediapipe/vision_tasks.hpp>

#include <memory>
#include <string>

namespace task_graph {

// 视觉结果公共类型的稳定名（跨 SO 一致；stub 构建同样注册，图可反序列化）
TG_REGISTER_TYPE(NormalizedLandmark, "task_graph::NormalizedLandmark")
TG_REGISTER_TYPE(Landmark, "task_graph::Landmark")
TG_REGISTER_TYPE(Detection, "task_graph::Detection")
TG_REGISTER_TYPE(Category, "task_graph::Category")
TG_REGISTER_TYPE(MatrixData, "task_graph::MatrixData")
TG_REGISTER_TYPE(SegmentationMask, "task_graph::SegmentationMask")
TG_REGISTER_TYPE(VisionResult, "task_graph::VisionResult")

namespace {

// 类型名常量（anonymous namespace，避免静态初始化顺序问题——AGENTS.md 约定）
const char* const kFaceLandmarkerType = "mp_face_landmarker";
const char* const kHandLandmarkerType = "mp_hand_landmarker";
const char* const kPoseLandmarkerType = "mp_pose_landmarker";
const char* const kObjectDetectorType = "mp_object_detector";
const char* const kFaceDetectorType = "mp_face_detector";
const char* const kImageClassifierType = "mp_image_classifier";
const char* const kImageEmbedderType = "mp_image_embedder";
const char* const kImageSegmenterType = "mp_image_segmenter";
const char* const kGestureRecognizerType = "mp_gesture_recognizer";
const char* const kHolisticLandmarkerType = "mp_holistic_landmarker";

bool do_register() {
    auto& reg = PluginRegistry::instance();
    reg.register_task(kFaceLandmarkerType,
        [](const std::string& id, const TaskConfig& cfg) {
            return std::make_shared<MpFaceLandmarkerTask>(id, cfg);
        });
    reg.register_task(kHandLandmarkerType,
        [](const std::string& id, const TaskConfig& cfg) {
            return std::make_shared<MpHandLandmarkerTask>(id, cfg);
        });
    reg.register_task(kPoseLandmarkerType,
        [](const std::string& id, const TaskConfig& cfg) {
            return std::make_shared<MpPoseLandmarkerTask>(id, cfg);
        });
    reg.register_task(kObjectDetectorType,
        [](const std::string& id, const TaskConfig& cfg) {
            return std::make_shared<MpObjectDetectorTask>(id, cfg);
        });
    reg.register_task(kFaceDetectorType,
        [](const std::string& id, const TaskConfig& cfg) {
            return std::make_shared<MpFaceDetectorTask>(id, cfg);
        });
    reg.register_task(kImageClassifierType,
        [](const std::string& id, const TaskConfig& cfg) {
            return std::make_shared<MpImageClassifierTask>(id, cfg);
        });
    reg.register_task(kImageEmbedderType,
        [](const std::string& id, const TaskConfig& cfg) {
            return std::make_shared<MpImageEmbedderTask>(id, cfg);
        });
    reg.register_task(kImageSegmenterType,
        [](const std::string& id, const TaskConfig& cfg) {
            return std::make_shared<MpImageSegmenterTask>(id, cfg);
        });
    reg.register_task(kGestureRecognizerType,
        [](const std::string& id, const TaskConfig& cfg) {
            return std::make_shared<MpGestureRecognizerTask>(id, cfg);
        });
    reg.register_task(kHolisticLandmarkerType,
        [](const std::string& id, const TaskConfig& cfg) {
            return std::make_shared<MpHolisticLandmarkerTask>(id, cfg);
        });
    return true;
}

void do_unregister() {
    auto& reg = PluginRegistry::instance();
    reg.unregister_task(kFaceLandmarkerType);
    reg.unregister_task(kHandLandmarkerType);
    reg.unregister_task(kPoseLandmarkerType);
    reg.unregister_task(kObjectDetectorType);
    reg.unregister_task(kFaceDetectorType);
    reg.unregister_task(kImageClassifierType);
    reg.unregister_task(kImageEmbedderType);
    reg.unregister_task(kImageSegmenterType);
    reg.unregister_task(kGestureRecognizerType);
    reg.unregister_task(kHolisticLandmarkerType);
}

TG_PLUGIN_AUTOREG(do_register, do_unregister)

}  // namespace

namespace detail {

// 非 inline anchor：STATIC 核心 lib（iOS/Android/WASM）下链接器会裁剪没有
// 外部引用的 TU。data_types.cpp 的 TypeRegistry::instance()（必然被引用）
// 在 TASK_GRAPH_MEDIAPIPE_AVAILABLE 下调用本函数，强制拉入本 TU —— 与
// pull_mnn_tasks() 同一手法。核心库直连注册不再有 dlopen 入口点同名碰撞面
//（WASM whole-archive 的 register_plugin 冲突问题随之消失）。
void pull_mediapipe_tasks();

}  // namespace detail

// 定义在本 TU（匿名空间里的 TG_PLUGIN_AUTOREG 静态初始化器随之被拉入）
namespace detail {
void pull_mediapipe_tasks() {}
}  // namespace detail

}  // namespace task_graph
