// MediaPipe Vision 任务层：参数解析 + 模型路径解析，推理全部委托
// 公共引擎 API MpVisionEngine（src/mediapipe/vision_engine.cpp）。
// 任务照常注册（图可反序列化/编辑）；引擎 stub 时 execute 返回 FAILED。
#include <task_graph/mediapipe/vision_tasks.hpp>

#include <task_graph/path_utils.hpp>

#include <memory>
#include <string>

#ifdef TASK_GRAPH_ENABLE_OPENCV
#include <opencv2/opencv.hpp>
#endif

namespace task_graph {

namespace {
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
}  // namespace

std::vector<PortSpec> MpVisionTaskNode::input_specs() const {
    return {PortSpec{"image", "", true}};
}

std::vector<PortSpec> MpVisionTaskNode::output_specs() const {
    // 推理结果以 VisionResult 结构化输出（单输出口 "out"）
    return {PortSpec{"out", "", false}};
}

std::vector<ParamSpec> MpVisionTaskNode::param_specs() const {
    return {
        make_file_param("model_path", "", "MediaPipe Task (*.task)"),
        make_enum_param("delegate", 0, {{"CPU", 0}, {"GPU", 1}}),
        make_enum_param("running_mode", 1,
            {{"IMAGE", 1}, {"VIDEO", 2}, {"LIVE_STREAM", 3}}),
    };
}

std::string MpVisionTaskNode::resolve_model_path(std::string& err) const {
    err.clear();
    const std::string raw = config().params.get_string("model_path").value_or("");
    if (raw.empty()) {
        err = "model_path not set";
        return "";
    }
    // 统一模型管理：宿主在 SDK 初始化时经 set_model_finder() 注入"模型名 →
    // 路径"回调，图里只需填模型名（如 "face_landmarker.task"）。
    if (const std::string hit = find_model(raw); !hit.empty()) {
        return hit;
    }
    // 回退：resolve_asset_path 先按 图目录+两级祖先 探测（源码树 tests/models
    // 在 graphs 兄弟目录，夹具图可直接拖进 GraphStudio），未命中回退纯拼接
    //（保住历史报错文案）——老 graph 与未装 finder 的宿主（WASM 等）不受影响。
    const std::string base_dir =
        config().params.get_string(kSourceDirParam).value_or("");
    return resolve_asset_path(base_dir, raw);
}

void MpVisionTaskNode::on_init() {
    std::string err;
    MpVisionOptions opts;
    opts.model_path = resolve_model_path(err);
    if (opts.model_path.empty()) {
        init_error_ = err;
        return;
    }
    opts.delegate = config().params.get_int("delegate").value_or(0);
    fill_options(opts);

    engine_ = MpVisionEngine::create(vision_task(), opts, init_error_);
    if (!engine_ && init_error_.empty()) {
        init_error_ = "MediaPipe engine create failed";
    }
}

TaskResult MpVisionTaskNode::execute(TaskContext& ctx) {
    if (!engine_) {
        TaskResult r;
        r.status = TaskStatus::FAILED;
        r.value = init_error_.empty()
                      ? std::string("MediaPipe task not initialized (model missing?)")
                      : init_error_;
        return r;
    }

    std::shared_ptr<Image> img_holder;
    if (auto img_opt = ctx.input<Image>("image")) {
        img_holder = std::make_shared<Image>(std::move(*img_opt));
    } else {
#ifdef TASK_GRAPH_ENABLE_OPENCV
        if (auto mat_opt = ctx.input<cv::Mat>("image")) {
            img_holder = std::make_shared<Image>(Image::from_mat(*mat_opt));
        }
#endif
    }
    if (!img_holder) {
        TaskResult r;
        r.status = TaskStatus::FAILED;
        r.value = std::string("missing image input on port 'image'");
        return r;
    }
    img_holder->ensure_cpu();
    if (!img_holder->valid()) {
        TaskResult r;
        r.status = TaskStatus::FAILED;
        r.value = std::string("invalid image data");
        return r;
    }

    VisionResult out;
    std::string err;
    if (!engine_->run(*img_holder, out, err)) {
        TaskResult r;
        r.status = TaskStatus::FAILED;
        r.value = err;
        return r;
    }

    out.task_type = type();
    TaskResult r;
    r.status = TaskStatus::COMPLETED;
    r.value = out;
    return r;
}

// ---- param_specs / fill_options（自原插件逐任务平移） ----

TG_DEFINE_MP_TASK(MpFaceLandmarkerTask, kFaceLandmarkerType)
std::vector<ParamSpec> MpFaceLandmarkerTask::param_specs() const {
    auto base = MpVisionTaskNode::param_specs();
    base.push_back(make_int_param("num_faces", 1, 1, 10));
    base.push_back(make_float_param("min_face_detection_confidence", 0.5f, 0.0, 1.0));
    base.push_back(make_float_param("min_face_presence_confidence", 0.5f, 0.0, 1.0));
    base.push_back(make_float_param("min_tracking_confidence", 0.5f, 0.0, 1.0));
    base.push_back(make_bool_param("output_face_blendshapes", false));
    base.push_back(make_bool_param("output_facial_transformation_matrixes", false));
    return base;
}
void MpFaceLandmarkerTask::fill_options(MpVisionOptions& opts) const {
    opts.num_results = config().params.get_int("num_faces").value_or(1);
    opts.min_detection_confidence =
        config().params.get_float("min_face_detection_confidence").value_or(0.5f);
    opts.min_presence_confidence =
        config().params.get_float("min_face_presence_confidence").value_or(0.5f);
    opts.min_tracking_confidence =
        config().params.get_float("min_tracking_confidence").value_or(0.5f);
    opts.output_face_blendshapes =
        config().params.get_bool("output_face_blendshapes").value_or(false);
    opts.output_transformation_matrixes =
        config().params.get_bool("output_facial_transformation_matrixes").value_or(false);
}

TG_DEFINE_MP_TASK(MpHandLandmarkerTask, kHandLandmarkerType)
std::vector<ParamSpec> MpHandLandmarkerTask::param_specs() const {
    auto base = MpVisionTaskNode::param_specs();
    base.push_back(make_int_param("num_hands", 1, 1, 10));
    base.push_back(make_float_param("min_hand_detection_confidence", 0.5f, 0.0, 1.0));
    base.push_back(make_float_param("min_hand_presence_confidence", 0.5f, 0.0, 1.0));
    base.push_back(make_float_param("min_tracking_confidence", 0.5f, 0.0, 1.0));
    return base;
}
void MpHandLandmarkerTask::fill_options(MpVisionOptions& opts) const {
    opts.num_results = config().params.get_int("num_hands").value_or(1);
    opts.min_detection_confidence =
        config().params.get_float("min_hand_detection_confidence").value_or(0.5f);
    opts.min_presence_confidence =
        config().params.get_float("min_hand_presence_confidence").value_or(0.5f);
    opts.min_tracking_confidence =
        config().params.get_float("min_tracking_confidence").value_or(0.5f);
}

TG_DEFINE_MP_TASK(MpPoseLandmarkerTask, kPoseLandmarkerType)
std::vector<ParamSpec> MpPoseLandmarkerTask::param_specs() const {
    auto base = MpVisionTaskNode::param_specs();
    base.push_back(make_int_param("num_poses", 1, 1, 10));
    base.push_back(make_float_param("min_pose_detection_confidence", 0.5f, 0.0, 1.0));
    base.push_back(make_float_param("min_pose_presence_confidence", 0.5f, 0.0, 1.0));
    base.push_back(make_float_param("min_tracking_confidence", 0.5f, 0.0, 1.0));
    base.push_back(make_bool_param("output_segmentation_masks", false));
    return base;
}
void MpPoseLandmarkerTask::fill_options(MpVisionOptions& opts) const {
    opts.num_results = config().params.get_int("num_poses").value_or(1);
    opts.min_detection_confidence =
        config().params.get_float("min_pose_detection_confidence").value_or(0.5f);
    opts.min_presence_confidence =
        config().params.get_float("min_pose_presence_confidence").value_or(0.5f);
    opts.min_tracking_confidence =
        config().params.get_float("min_tracking_confidence").value_or(0.5f);
    opts.output_segmentation_masks =
        config().params.get_bool("output_segmentation_masks").value_or(false);
}

TG_DEFINE_MP_TASK(MpObjectDetectorTask, kObjectDetectorType)
std::vector<ParamSpec> MpObjectDetectorTask::param_specs() const {
    auto base = MpVisionTaskNode::param_specs();
    base.push_back(make_int_param("max_results", -1, -1, 100));
    base.push_back(make_float_param("score_threshold", 0.0f, 0.0, 1.0));
    return base;
}
void MpObjectDetectorTask::fill_options(MpVisionOptions& opts) const {
    opts.max_results = config().params.get_int("max_results").value_or(-1);
    opts.score_threshold = config().params.get_float("score_threshold").value_or(0.0f);
}

TG_DEFINE_MP_TASK(MpFaceDetectorTask, kFaceDetectorType)
std::vector<ParamSpec> MpFaceDetectorTask::param_specs() const {
    auto base = MpVisionTaskNode::param_specs();
    base.push_back(make_float_param("min_detection_confidence", 0.5f, 0.0, 1.0));
    base.push_back(make_float_param("min_suppression_threshold", 0.5f, 0.0, 1.0));
    return base;
}
void MpFaceDetectorTask::fill_options(MpVisionOptions& opts) const {
    opts.min_detection_confidence =
        config().params.get_float("min_detection_confidence").value_or(0.5f);
    opts.min_suppression_threshold =
        config().params.get_float("min_suppression_threshold").value_or(0.5f);
}

TG_DEFINE_MP_TASK(MpImageClassifierTask, kImageClassifierType)
std::vector<ParamSpec> MpImageClassifierTask::param_specs() const {
    auto base = MpVisionTaskNode::param_specs();
    base.push_back(make_int_param("max_results", -1, -1, 100));
    base.push_back(make_float_param("score_threshold", 0.0f, 0.0, 1.0));
    return base;
}
void MpImageClassifierTask::fill_options(MpVisionOptions& opts) const {
    opts.max_results = config().params.get_int("max_results").value_or(-1);
    opts.score_threshold = config().params.get_float("score_threshold").value_or(0.0f);
}

TG_DEFINE_MP_TASK(MpImageEmbedderTask, kImageEmbedderType)
std::vector<ParamSpec> MpImageEmbedderTask::param_specs() const {
    auto base = MpVisionTaskNode::param_specs();
    base.push_back(make_bool_param("l2_normalize", false));
    // Scalar-quantization output is not mapped; quantize=false keeps float.
    return base;
}
void MpImageEmbedderTask::fill_options(MpVisionOptions& opts) const {
    opts.l2_normalize = config().params.get_bool("l2_normalize").value_or(false);
}

TG_DEFINE_MP_TASK(MpImageSegmenterTask, kImageSegmenterType)
std::vector<ParamSpec> MpImageSegmenterTask::param_specs() const {
    auto base = MpVisionTaskNode::param_specs();
    base.push_back(make_bool_param("output_confidence_masks", true));
    base.push_back(make_bool_param("output_category_mask", false));
    return base;
}
void MpImageSegmenterTask::fill_options(MpVisionOptions& opts) const {
    opts.output_confidence_masks =
        config().params.get_bool("output_confidence_masks").value_or(true);
    opts.output_category_mask =
        config().params.get_bool("output_category_mask").value_or(false);
}

TG_DEFINE_MP_TASK(MpGestureRecognizerTask, kGestureRecognizerType)
std::vector<ParamSpec> MpGestureRecognizerTask::param_specs() const {
    auto base = MpVisionTaskNode::param_specs();
    base.push_back(make_int_param("num_hands", 1, 1, 10));
    base.push_back(make_float_param("min_hand_detection_confidence", 0.5f, 0.0, 1.0));
    base.push_back(make_float_param("min_hand_presence_confidence", 0.5f, 0.0, 1.0));
    base.push_back(make_float_param("min_tracking_confidence", 0.5f, 0.0, 1.0));
    return base;
}
void MpGestureRecognizerTask::fill_options(MpVisionOptions& opts) const {
    opts.num_results = config().params.get_int("num_hands").value_or(1);
    opts.min_detection_confidence =
        config().params.get_float("min_hand_detection_confidence").value_or(0.5f);
    opts.min_presence_confidence =
        config().params.get_float("min_hand_presence_confidence").value_or(0.5f);
    opts.min_tracking_confidence =
        config().params.get_float("min_tracking_confidence").value_or(0.5f);
}

TG_DEFINE_MP_TASK(MpHolisticLandmarkerTask, kHolisticLandmarkerType)
std::vector<ParamSpec> MpHolisticLandmarkerTask::param_specs() const {
    auto base = MpVisionTaskNode::param_specs();
    base.push_back(make_bool_param("output_face_blendshapes", false));
    base.push_back(make_bool_param("output_pose_segmentation_masks", false));
    return base;
}
void MpHolisticLandmarkerTask::fill_options(MpVisionOptions& opts) const {
    opts.output_face_blendshapes =
        config().params.get_bool("output_face_blendshapes").value_or(false);
    opts.output_segmentation_masks =
        config().params.get_bool("output_pose_segmentation_masks").value_or(false);
}

}  // namespace task_graph
