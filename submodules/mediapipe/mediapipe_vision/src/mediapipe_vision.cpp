#include <mediapipe_vision/mediapipe_vision.hpp>

#include <cstdlib>
#include <memory>
#include <string>

#ifdef TASK_GRAPH_ENABLE_OPENCV
#include <opencv2/opencv.hpp>
#endif

#ifdef MEDIAPIPE_VISION_AVAILABLE
#include "mediapipe/tasks/c/core/mp_status.h"
#include "mediapipe/tasks/c/vision/core/image.h"
#endif

TG_REGISTER_TYPE(task_graph::NormalizedLandmark, "task_graph::NormalizedLandmark")
TG_REGISTER_TYPE(task_graph::Landmark, "task_graph::Landmark")
TG_REGISTER_TYPE(task_graph::Detection, "task_graph::Detection")
TG_REGISTER_TYPE(task_graph::VisionResult, "task_graph::VisionResult")

namespace {
const char* const kFaceLandmarkerType = "mp_face_landmarker";
const char* const kHandLandmarkerType = "mp_hand_landmarker";
const char* const kPoseLandmarkerType = "mp_pose_landmarker";
const char* const kObjectDetectorType = "mp_object_detector";
}  // namespace

namespace mediapipe_vision {

std::vector<task_graph::PortSpec> MediaPipeVisionTaskBase::input_specs() const {
    return {task_graph::PortSpec{"image", "", true}};
}

std::vector<task_graph::ParamSpec> MediaPipeVisionTaskBase::param_specs() const {
    return {
        task_graph::make_file_param("model_path", "", "MediaPipe Task (*.task)"),
        task_graph::make_enum_param("delegate", 0, {{"CPU", 0}, {"GPU", 1}}),
        task_graph::make_enum_param("running_mode", 1,
            {{"IMAGE", 1}, {"VIDEO", 2}, {"LIVE_STREAM", 3}}),
    };
}

void MediaPipeVisionTaskBase::on_init() {}

task_graph::TaskResult MediaPipeVisionTaskBase::failed(const std::string& msg) {
    task_graph::TaskResult r;
    r.status = task_graph::TaskStatus::FAILED;
    r.value = msg;
    return r;
}

task_graph::TaskResult MediaPipeVisionTaskBase::run_image(
    task_graph::TaskContext& ctx, task_graph::VisionResult& out) {
    std::shared_ptr<task_graph::Image> img_holder;

    if (auto img_opt = ctx.template input<task_graph::Image>("image")) {
        img_holder = std::make_shared<task_graph::Image>(std::move(*img_opt));
    } else {
#ifdef TASK_GRAPH_ENABLE_OPENCV
        if (auto mat_opt = ctx.template input<cv::Mat>("image")) {
            img_holder = std::make_shared<task_graph::Image>(
                task_graph::Image::from_mat(*mat_opt));
        }
#endif
    }

    if (!img_holder) {
        return failed("missing image input on port 'image'");
    }

    img_holder->ensure_cpu();
    if (!img_holder->valid()) {
        return failed("invalid image data");
    }

#ifdef MEDIAPIPE_VISION_AVAILABLE
    MpImageFormat fmt = kMpImageFormatSrgb;
    if (img_holder->channels == 1) {
        fmt = kMpImageFormatGray8;
    } else if (img_holder->channels == 4) {
        fmt = kMpImageFormatSrgba;
    } else if (img_holder->channels == 3) {
        fmt = kMpImageFormatSrgb;
    }

    MpImagePtr mp_img = nullptr;
    char* err_msg = nullptr;
    MpStatus st = MpImageCreateFromUint8Data(
        fmt, img_holder->width, img_holder->height, img_holder->ptr(),
        static_cast<int>(img_holder->total_size()), &mp_img, &err_msg);
    if (st != kMpOk) {
        if (err_msg) std::free(err_msg);
        return failed("MpImageCreateFromUint8Data failed");
    }

    // TODO: invoke FaceLandmarkerCreate/DetectImage/Close (fill `out`)

    MpImageFree(mp_img);
    if (err_msg) std::free(err_msg);
#else
    // stub: MediaPipe prebuilt not linked, returning empty result
#endif

    out.task_type = type();
    task_graph::TaskResult r;
    r.status = task_graph::TaskStatus::COMPLETED;
    r.value = out;
    return r;
}

// ---- FaceLandmarkerTask ----

const std::string& FaceLandmarkerTask::type() const {
    static const std::string t(kFaceLandmarkerType);
    return t;
}

task_graph::TaskResult FaceLandmarkerTask::execute(task_graph::TaskContext& ctx) {
    task_graph::VisionResult result;
    return run_image(ctx, result);
}

std::vector<task_graph::ParamSpec> FaceLandmarkerTask::param_specs() const {
    auto base = MediaPipeVisionTaskBase::param_specs();
    base.push_back(task_graph::make_int_param("num_faces", 1, 1, 10));
    base.push_back(task_graph::make_float_param(
        "min_face_detection_confidence", 0.5f, 0.0, 1.0));
    base.push_back(task_graph::make_float_param(
        "min_face_presence_confidence", 0.5f, 0.0, 1.0));
    base.push_back(task_graph::make_float_param(
        "min_tracking_confidence", 0.5f, 0.0, 1.0));
    base.push_back(task_graph::make_bool_param("output_face_blendshapes", false));
    base.push_back(task_graph::make_bool_param(
        "output_facial_transformation_matrixes", false));
    return base;
}

// ---- HandLandmarkerTask ----

const std::string& HandLandmarkerTask::type() const {
    static const std::string t(kHandLandmarkerType);
    return t;
}

task_graph::TaskResult HandLandmarkerTask::execute(task_graph::TaskContext& ctx) {
    task_graph::VisionResult result;
    return run_image(ctx, result);
}

std::vector<task_graph::ParamSpec> HandLandmarkerTask::param_specs() const {
    auto base = MediaPipeVisionTaskBase::param_specs();
    base.push_back(task_graph::make_int_param("num_hands", 1, 1, 10));
    base.push_back(task_graph::make_float_param(
        "min_hand_detection_confidence", 0.5f, 0.0, 1.0));
    base.push_back(task_graph::make_float_param(
        "min_hand_presence_confidence", 0.5f, 0.0, 1.0));
    base.push_back(task_graph::make_float_param(
        "min_tracking_confidence", 0.5f, 0.0, 1.0));
    return base;
}

// ---- PoseLandmarkerTask ----

const std::string& PoseLandmarkerTask::type() const {
    static const std::string t(kPoseLandmarkerType);
    return t;
}

task_graph::TaskResult PoseLandmarkerTask::execute(task_graph::TaskContext& ctx) {
    task_graph::VisionResult result;
    return run_image(ctx, result);
}

std::vector<task_graph::ParamSpec> PoseLandmarkerTask::param_specs() const {
    auto base = MediaPipeVisionTaskBase::param_specs();
    base.push_back(task_graph::make_int_param("num_poses", 1, 1, 10));
    base.push_back(task_graph::make_float_param(
        "min_pose_detection_confidence", 0.5f, 0.0, 1.0));
    base.push_back(task_graph::make_float_param(
        "min_pose_presence_confidence", 0.5f, 0.0, 1.0));
    base.push_back(task_graph::make_float_param(
        "min_tracking_confidence", 0.5f, 0.0, 1.0));
    return base;
}

// ---- ObjectDetectorTask ----

const std::string& ObjectDetectorTask::type() const {
    static const std::string t(kObjectDetectorType);
    return t;
}

task_graph::TaskResult ObjectDetectorTask::execute(task_graph::TaskContext& ctx) {
    task_graph::VisionResult result;
    return run_image(ctx, result);
}

std::vector<task_graph::ParamSpec> ObjectDetectorTask::param_specs() const {
    auto base = MediaPipeVisionTaskBase::param_specs();
    base.push_back(task_graph::make_int_param("max_results", -1, -1, 100));
    base.push_back(task_graph::make_float_param("score_threshold", 0.0f, 0.0, 1.0));
    return base;
}

}  // namespace mediapipe_vision

namespace {

bool do_register() {
    auto& reg = task_graph::PluginRegistry::instance();
    reg.register_task(kFaceLandmarkerType,
        [](const std::string& id, const task_graph::TaskConfig& cfg) {
            return std::make_shared<mediapipe_vision::FaceLandmarkerTask>(id, cfg);
        });
    reg.register_task(kHandLandmarkerType,
        [](const std::string& id, const task_graph::TaskConfig& cfg) {
            return std::make_shared<mediapipe_vision::HandLandmarkerTask>(id, cfg);
        });
    reg.register_task(kPoseLandmarkerType,
        [](const std::string& id, const task_graph::TaskConfig& cfg) {
            return std::make_shared<mediapipe_vision::PoseLandmarkerTask>(id, cfg);
        });
    reg.register_task(kObjectDetectorType,
        [](const std::string& id, const task_graph::TaskConfig& cfg) {
            return std::make_shared<mediapipe_vision::ObjectDetectorTask>(id, cfg);
        });
    return true;
}

void do_unregister() {
    auto& reg = task_graph::PluginRegistry::instance();
    reg.unregister_task(kFaceLandmarkerType);
    reg.unregister_task(kHandLandmarkerType);
    reg.unregister_task(kPoseLandmarkerType);
    reg.unregister_task(kObjectDetectorType);
}

__attribute__((constructor))
static void mediapipe_vision_constructor() {
    do_register();
}

__attribute__((destructor))
static void mediapipe_vision_destructor() {
    do_unregister();
}

}  // namespace

extern "C" TG_EXPORT bool register_plugin() {
    return do_register();
}

extern "C" TG_EXPORT void unregister_plugin() {
    do_unregister();
}
