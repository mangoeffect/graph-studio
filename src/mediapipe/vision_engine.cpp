// MediaPipe Vision 引擎实现（pimpl）：全部 Mp* C API 调用集中于此。
// TASK_GRAPH_MEDIAPIPE_AVAILABLE 时为真实实现；否则 create() 恒失败（stub）。
#include <task_graph/mediapipe/vision_engine.hpp>

#include <cstdlib>

#ifdef TASK_GRAPH_MEDIAPIPE_AVAILABLE

#include "mediapipe/tasks/c/components/containers/category.h"
#include "mediapipe/tasks/c/components/containers/classification_result.h"
#include "mediapipe/tasks/c/components/containers/detection_result.h"
#include "mediapipe/tasks/c/components/containers/embedding_result.h"
#include "mediapipe/tasks/c/components/containers/landmark.h"
#include "mediapipe/tasks/c/components/containers/matrix.h"
#include "mediapipe/tasks/c/core/mp_status.h"
#include "mediapipe/tasks/c/vision/core/image.h"
#include "mediapipe/tasks/c/vision/face_detector/face_detector.h"
#include "mediapipe/tasks/c/vision/face_landmarker/face_landmarker.h"
#include "mediapipe/tasks/c/vision/face_landmarker/face_landmarker_result.h"
#include "mediapipe/tasks/c/vision/gesture_recognizer/gesture_recognizer.h"
#include "mediapipe/tasks/c/vision/gesture_recognizer/gesture_recognizer_result.h"
#include "mediapipe/tasks/c/vision/hand_landmarker/hand_landmarker.h"
#include "mediapipe/tasks/c/vision/hand_landmarker/hand_landmarker_result.h"
#include "mediapipe/tasks/c/vision/holistic_landmarker/holistic_landmarker.h"
#include "mediapipe/tasks/c/vision/holistic_landmarker/holistic_landmarker_result.h"
#include "mediapipe/tasks/c/vision/image_classifier/image_classifier.h"
#include "mediapipe/tasks/c/vision/image_embedder/image_embedder.h"
#include "mediapipe/tasks/c/vision/image_segmenter/image_segmenter.h"
#include "mediapipe/tasks/c/vision/image_segmenter/image_segmenter_result.h"
#include "mediapipe/tasks/c/vision/object_detector/object_detector.h"
#include "mediapipe/tasks/c/vision/pose_landmarker/pose_landmarker.h"
#include "mediapipe/tasks/c/vision/pose_landmarker/pose_landmarker_result.h"

namespace task_graph {
namespace {

// ---- C 结果 → 公共结构映射（自原插件 mediapipe_vision.cpp 平移） ----

void map_norm_landmarks(const ::MpNormalizedLandmarks* src, uint32_t count,
                        std::vector<std::vector<NormalizedLandmark>>& dst) {
    if (!src) return;
    dst.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const ::MpNormalizedLandmarks& grp = src[i];
        std::vector<NormalizedLandmark> pts;
        if (grp.landmarks && grp.landmarks_count > 0) {
            pts.reserve(grp.landmarks_count);
            for (uint32_t j = 0; j < grp.landmarks_count; ++j) {
                const ::MpNormalizedLandmark& s = grp.landmarks[j];
                NormalizedLandmark d;
                d.x = s.x; d.y = s.y; d.z = s.z;
                d.visibility = s.visibility; d.presence = s.presence;
                if (s.name) d.name = s.name;
                pts.push_back(std::move(d));
            }
        }
        dst.push_back(std::move(pts));
    }
}

void map_world_landmarks(const ::MpLandmarks* src, uint32_t count,
                         std::vector<std::vector<Landmark>>& dst) {
    if (!src) return;
    dst.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const ::MpLandmarks& grp = src[i];
        std::vector<Landmark> pts;
        if (grp.landmarks && grp.landmarks_count > 0) {
            pts.reserve(grp.landmarks_count);
            for (uint32_t j = 0; j < grp.landmarks_count; ++j) {
                const ::MpLandmark& s = grp.landmarks[j];
                Landmark d;
                d.x = s.x; d.y = s.y; d.z = s.z;
                d.visibility = s.visibility; d.presence = s.presence;
                if (s.name) d.name = s.name;
                pts.push_back(std::move(d));
            }
        }
        dst.push_back(std::move(pts));
    }
}

void map_categories(const ::MpCategories* src, uint32_t count,
                    std::vector<std::vector<Category>>& dst) {
    if (!src) return;
    dst.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const ::MpCategories& grp = src[i];
        std::vector<Category> cats;
        if (grp.categories && grp.categories_count > 0) {
            cats.reserve(grp.categories_count);
            for (uint32_t j = 0; j < grp.categories_count; ++j) {
                const ::MpCategory& s = grp.categories[j];
                Category c;
                c.index = s.index; c.score = s.score;
                if (s.category_name) c.category_name = s.category_name;
                if (s.display_name) c.display_name = s.display_name;
                cats.push_back(std::move(c));
            }
        }
        dst.push_back(std::move(cats));
    }
}

void map_matrix(const ::MpMatrix& src, MatrixData& dst) {
    dst.rows = src.rows;
    dst.cols = src.cols;
    if (src.data && src.rows > 0 && src.cols > 0) {
        dst.data.assign(src.data, src.data + static_cast<size_t>(src.rows) * src.cols);
    }
}

bool map_mask(MpImagePtr mp_img, SegmentationMask& out) {
    int w = MpImageGetWidth(mp_img);
    int h = MpImageGetHeight(mp_img);
    if (w <= 0 || h <= 0) return false;
    const float* data = nullptr;
    char* e = nullptr;
    MpStatus st = MpImageDataFloat32(mp_img, &data, &e);
    if (e) std::free(e);
    if (st != kMpOk || !data) return false;
    out.width = w;
    out.height = h;
    out.data.assign(data, data + static_cast<size_t>(w) * h);
    return true;
}

bool map_mask_uint8(MpImagePtr mp_img, SegmentationCategoryMask& out) {
    int w = MpImageGetWidth(mp_img);
    int h = MpImageGetHeight(mp_img);
    if (w <= 0 || h <= 0) return false;
    const uint8_t* data = nullptr;
    char* e = nullptr;
    MpStatus st = MpImageDataUint8(mp_img, &data, &e);
    if (e) std::free(e);
    if (st != kMpOk || !data) return false;
    out.width = w;
    out.height = h;
    out.data.assign(data, data + static_cast<size_t>(w) * h);
    return true;
}

void map_detections(const ::MpDetection* src, uint32_t count, MpImagePtr mp_img,
                    std::vector<Detection>& dst) {
    if (!src) return;
    float w = static_cast<float>(MpImageGetWidth(mp_img));
    float h = static_cast<float>(MpImageGetHeight(mp_img));
    if (w <= 0) w = 1.0f;
    if (h <= 0) h = 1.0f;
    dst.reserve(dst.size() + count);
    for (uint32_t i = 0; i < count; ++i) {
        const ::MpDetection& d = src[i];
        Detection det;
        for (uint32_t j = 0; j < d.categories_count; ++j) {
            const ::MpCategory& c = d.categories[j];
            det.scores.push_back(c.score);
            det.indices.push_back(c.index);
            if (c.category_name) {
                det.labels.push_back(c.category_name);
            } else {
                det.labels.emplace_back();
            }
        }
        det.x_min = static_cast<float>(d.bounding_box.left) / w;
        det.y_min = static_cast<float>(d.bounding_box.top) / h;
        det.x_max = static_cast<float>(d.bounding_box.right) / w;
        det.y_max = static_cast<float>(d.bounding_box.bottom) / h;
        dst.push_back(std::move(det));
    }
}

void map_embeddings(const ::MpEmbedding* src, uint32_t count,
                    std::vector<EmbeddingValue>& dst) {
    if (!src) return;
    dst.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const ::MpEmbedding& s = src[i];
        EmbeddingValue e;
        e.head_index = s.head_index;
        if (s.float_embedding && s.values_count > 0) {
            e.values.assign(s.float_embedding, s.float_embedding + s.values_count);
        }
        dst.push_back(std::move(e));
    }
}

std::string take_err(char* err_msg) {
    std::string s;
    if (err_msg) {
        s.assign(err_msg);
        std::free(err_msg);
    }
    return s;
}

}  // namespace

struct MpVisionEngine::Impl {
    MpVisionTask task{MpVisionTask::FaceLandmarker};
    // 同一时刻只有一个联合句柄有效（由 task 决定哪个）。
    MpFaceLandmarkerPtr face_landmarker = nullptr;
    MpHandLandmarkerPtr hand_landmarker = nullptr;
    MpPoseLandmarkerPtr pose_landmarker = nullptr;
    MpObjectDetectorPtr object_detector = nullptr;
    MpFaceDetectorPtr face_detector = nullptr;
    MpImageClassifierPtr image_classifier = nullptr;
    MpImageEmbedderPtr image_embedder = nullptr;
    MpImageSegmenterPtr image_segmenter = nullptr;
    MpGestureRecognizerPtr gesture_recognizer = nullptr;
    MpHolisticLandmarkerPtr holistic_landmarker = nullptr;

    void close() {
        char* e = nullptr;
#define TG_MP_CLOSE(fn, h) \
        if (h) { fn(h, &e); h = nullptr; }
        TG_MP_CLOSE(MpFaceLandmarkerClose, face_landmarker)
        TG_MP_CLOSE(MpHandLandmarkerClose, hand_landmarker)
        TG_MP_CLOSE(MpPoseLandmarkerClose, pose_landmarker)
        TG_MP_CLOSE(MpObjectDetectorClose, object_detector)
        TG_MP_CLOSE(MpFaceDetectorClose, face_detector)
        TG_MP_CLOSE(MpImageClassifierClose, image_classifier)
        TG_MP_CLOSE(MpImageEmbedderClose, image_embedder)
        TG_MP_CLOSE(MpImageSegmenterClose, image_segmenter)
        TG_MP_CLOSE(MpGestureRecognizerClose, gesture_recognizer)
        TG_MP_CLOSE(MpHolisticLandmarkerClose, holistic_landmarker)
#undef TG_MP_CLOSE
        if (e) std::free(e);
    }
};

MpVisionEngine::MpVisionEngine() = default;
MpVisionEngine::~MpVisionEngine() = default;

bool MpVisionEngine::gpu_delegate_available() {
#ifdef TASK_GRAPH_MEDIAPIPE_CPU_ONLY
    return false;
#else
    return true;
#endif
}

std::shared_ptr<MpVisionEngine> MpVisionEngine::create(MpVisionTask task,
                                                       const MpVisionOptions& opts,
                                                       std::string& err) {
    if (opts.model_path.empty()) {
        err = "MediaPipe: model_path is empty";
        return nullptr;
    }
    if (opts.delegate == 1 && !gpu_delegate_available()) {
        err = "GPU delegate unavailable: prebuilt compiled with MEDIAPIPE_DISABLE_GPU";
        return nullptr;
    }

    auto engine = std::shared_ptr<MpVisionEngine>(new MpVisionEngine());
    engine->impl_ = std::make_unique<Impl>();
    Impl& impl = *engine->impl_;
    impl.task = task;

    const MpDelegate delegate = static_cast<MpDelegate>(opts.delegate);
    char* err_msg = nullptr;
    MpStatus st = kMpOk;

    switch (task) {
        case MpVisionTask::FaceLandmarker: {
            MpFaceLandmarkerOptions o{};
            o.base_options.model_asset_path = opts.model_path.c_str();
            o.base_options.delegate = delegate;
            o.running_mode = MP_RUNNING_MODE_IMAGE;
            o.num_faces = opts.num_results;
            o.min_face_detection_confidence = opts.min_detection_confidence;
            o.min_face_presence_confidence = opts.min_presence_confidence;
            o.min_tracking_confidence = opts.min_tracking_confidence;
            o.output_face_blendshapes = opts.output_face_blendshapes;
            o.output_facial_transformation_matrixes =
                opts.output_transformation_matrixes;
            o.result_callback = nullptr;
            st = MpFaceLandmarkerCreate(&o, &impl.face_landmarker, &err_msg);
            break;
        }
        case MpVisionTask::HandLandmarker: {
            MpHandLandmarkerOptions o{};
            o.base_options.model_asset_path = opts.model_path.c_str();
            o.base_options.delegate = delegate;
            o.running_mode = MP_RUNNING_MODE_IMAGE;
            o.num_hands = opts.num_results;
            o.min_hand_detection_confidence = opts.min_detection_confidence;
            o.min_hand_presence_confidence = opts.min_presence_confidence;
            o.min_tracking_confidence = opts.min_tracking_confidence;
            o.result_callback = nullptr;
            st = MpHandLandmarkerCreate(&o, &impl.hand_landmarker, &err_msg);
            break;
        }
        case MpVisionTask::PoseLandmarker: {
            MpPoseLandmarkerOptions o{};
            o.base_options.model_asset_path = opts.model_path.c_str();
            o.base_options.delegate = delegate;
            o.running_mode = MP_RUNNING_MODE_IMAGE;
            o.num_poses = opts.num_results;
            o.min_pose_detection_confidence = opts.min_detection_confidence;
            o.min_pose_presence_confidence = opts.min_presence_confidence;
            o.min_tracking_confidence = opts.min_tracking_confidence;
            o.output_segmentation_masks = opts.output_segmentation_masks;
            o.result_callback = nullptr;
            st = MpPoseLandmarkerCreate(&o, &impl.pose_landmarker, &err_msg);
            break;
        }
        case MpVisionTask::ObjectDetector: {
            MpObjectDetectorOptions o{};
            o.base_options.model_asset_path = opts.model_path.c_str();
            o.base_options.delegate = delegate;
            o.running_mode = MP_RUNNING_MODE_IMAGE;
            o.display_names_locale = nullptr;
            o.max_results = opts.max_results;
            o.score_threshold = opts.score_threshold;
            o.category_allowlist = nullptr;
            o.category_allowlist_count = 0;
            o.category_denylist = nullptr;
            o.category_denylist_count = 0;
            o.result_callback = nullptr;
            st = MpObjectDetectorCreate(&o, &impl.object_detector, &err_msg);
            break;
        }
        case MpVisionTask::FaceDetector: {
            MpFaceDetectorOptions o{};
            o.base_options.model_asset_path = opts.model_path.c_str();
            o.base_options.delegate = delegate;
            o.running_mode = MP_RUNNING_MODE_IMAGE;
            o.min_detection_confidence = opts.min_detection_confidence;
            o.min_suppression_threshold = opts.min_suppression_threshold;
            o.result_callback = nullptr;
            st = MpFaceDetectorCreate(&o, &impl.face_detector, &err_msg);
            break;
        }
        case MpVisionTask::ImageClassifier: {
            MpImageClassifierOptions o{};
            o.base_options.model_asset_path = opts.model_path.c_str();
            o.base_options.delegate = delegate;
            o.running_mode = MP_RUNNING_MODE_IMAGE;
            o.classifier_options.display_names_locale = nullptr;
            o.classifier_options.max_results = opts.max_results;
            o.classifier_options.score_threshold = opts.score_threshold;
            o.classifier_options.category_allowlist = nullptr;
            o.classifier_options.category_allowlist_count = 0;
            o.classifier_options.category_denylist = nullptr;
            o.classifier_options.category_denylist_count = 0;
            o.result_callback = nullptr;
            st = MpImageClassifierCreate(&o, &impl.image_classifier, &err_msg);
            break;
        }
        case MpVisionTask::ImageEmbedder: {
            ImageEmbedderOptions o{};
            o.base_options.model_asset_path = opts.model_path.c_str();
            o.base_options.delegate = delegate;
            o.running_mode = MP_RUNNING_MODE_IMAGE;
            o.embedder_options.l2_normalize = opts.l2_normalize;
            o.embedder_options.quantize = false;
            o.result_callback = nullptr;
            st = MpImageEmbedderCreate(&o, &impl.image_embedder, &err_msg);
            break;
        }
        case MpVisionTask::ImageSegmenter: {
            MpImageSegmenterOptions o{};
            o.base_options.model_asset_path = opts.model_path.c_str();
            o.base_options.delegate = delegate;
            o.running_mode = MP_RUNNING_MODE_IMAGE;
            // C-API quirk：CppConvertToImageSegmenterOptions 对该字段无空指针
            // 守卫，传 nullptr 会段错误——必须给非空串。
            o.display_names_locale = "en";
            o.output_confidence_masks = opts.output_confidence_masks;
            o.output_category_mask = opts.output_category_mask;
            o.result_callback = nullptr;
            st = MpImageSegmenterCreate(&o, &impl.image_segmenter, &err_msg);
            break;
        }
        case MpVisionTask::GestureRecognizer: {
            MpGestureRecognizerOptions o{};
            o.base_options.model_asset_path = opts.model_path.c_str();
            o.base_options.delegate = delegate;
            o.running_mode = MP_RUNNING_MODE_IMAGE;
            o.num_hands = opts.num_results;
            o.min_hand_detection_confidence = opts.min_detection_confidence;
            o.min_hand_presence_confidence = opts.min_presence_confidence;
            o.min_tracking_confidence = opts.min_tracking_confidence;
            // max_results = 0 非法，固定 -1（全量）。
            o.canned_gestures_classifier_options.display_names_locale = nullptr;
            o.canned_gestures_classifier_options.max_results = -1;
            o.canned_gestures_classifier_options.score_threshold = 0.0f;
            o.custom_gestures_classifier_options.display_names_locale = nullptr;
            o.custom_gestures_classifier_options.max_results = -1;
            o.custom_gestures_classifier_options.score_threshold = 0.0f;
            o.result_callback = nullptr;
            st = MpGestureRecognizerCreate(&o, &impl.gesture_recognizer, &err_msg);
            break;
        }
        case MpVisionTask::HolisticLandmarker: {
            MpHolisticLandmarkerOptions o{};
            o.base_options.model_asset_path = opts.model_path.c_str();
            o.base_options.delegate = delegate;
            o.running_mode = MP_RUNNING_MODE_IMAGE;
            o.output_face_blendshapes = opts.output_face_blendshapes;
            o.output_pose_segmentation_masks = opts.output_segmentation_masks;
            o.result_callback = nullptr;
            st = MpHolisticLandmarkerCreate(&o, &impl.holistic_landmarker, &err_msg);
            break;
        }
    }

    if (st != kMpOk) {
        impl.close();
        err = "MediaPipe task create failed";
        const std::string detail = take_err(err_msg);
        if (!detail.empty()) err += ": " + detail;
        return nullptr;
    }
    if (err_msg) std::free(err_msg);
    return engine;
}

bool MpVisionEngine::run(const uint8_t* data, int width, int height, int channels,
                         VisionResult& out, std::string& err) {
    if (!impl_) {
        err = "MediaPipe: engine not initialized";
        return false;
    }
    if (!data || width <= 0 || height <= 0) {
        err = "MediaPipe: invalid image data";
        return false;
    }

    Impl& impl = *impl_;
    MpImageFormat fmt = kMpImageFormatSrgb;
    if (channels == 1) {
        fmt = kMpImageFormatGray8;
    } else if (channels == 4) {
        fmt = kMpImageFormatSrgba;
    } else if (channels == 3) {
        fmt = kMpImageFormatSrgb;
    } else {
        err = "MediaPipe: unsupported channel count " + std::to_string(channels);
        return false;
    }

    MpImagePtr mp_img = nullptr;
    char* err_msg = nullptr;
    const size_t total = static_cast<size_t>(width) * height * channels;
    MpStatus st = MpImageCreateFromUint8Data(fmt, width, height, data,
                                             static_cast<int>(total), &mp_img,
                                             &err_msg);
    if (st != kMpOk) {
        const std::string detail = take_err(err_msg);
        err = "MpImageCreateFromUint8Data failed";
        if (!detail.empty()) err += ": " + detail;
        return false;
    }

    bool ok = false;
    switch (impl.task) {
        case MpVisionTask::FaceLandmarker: {
            if (!impl.face_landmarker) break;
            MpFaceLandmarkerResult result{};
            st = MpFaceLandmarkerDetectImage(impl.face_landmarker, mp_img,
                                             nullptr, &result, &err_msg);
            if (st == kMpOk) {
                map_norm_landmarks(result.face_landmarks,
                                   result.face_landmarks_count, out.face_landmarks);
                if (result.face_blendshapes_count > 0 && result.face_blendshapes) {
                    map_categories(result.face_blendshapes,
                                   result.face_blendshapes_count,
                                   out.face_blendshapes);
                }
                if (result.facial_transformation_matrixes_count > 0 &&
                    result.facial_transformation_matrixes) {
                    for (uint32_t i = 0;
                         i < result.facial_transformation_matrixes_count; ++i) {
                        MatrixData m;
                        map_matrix(result.facial_transformation_matrixes[i], m);
                        out.face_transformation_matrixes.push_back(std::move(m));
                    }
                }
                MpFaceLandmarkerCloseResult(&result);
                ok = true;
            } else {
                err = "MpFaceLandmarkerDetectImage failed";
            }
            break;
        }
        case MpVisionTask::HandLandmarker: {
            if (!impl.hand_landmarker) break;
            MpHandLandmarkerResult result{};
            st = MpHandLandmarkerDetectImage(impl.hand_landmarker, mp_img,
                                             nullptr, &result, &err_msg);
            if (st == kMpOk) {
                map_norm_landmarks(result.hand_landmarks,
                                   result.hand_landmarks_count, out.hand_landmarks);
                map_world_landmarks(result.hand_world_landmarks,
                                    result.hand_world_landmarks_count,
                                    out.hand_world_landmarks);
                map_categories(result.handedness, result.handedness_count,
                               out.handedness);
                MpHandLandmarkerCloseResult(&result);
                ok = true;
            } else {
                err = "MpHandLandmarkerDetectImage failed";
            }
            break;
        }
        case MpVisionTask::PoseLandmarker: {
            if (!impl.pose_landmarker) break;
            MpPoseLandmarkerResult result{};
            st = MpPoseLandmarkerDetectImage(impl.pose_landmarker, mp_img,
                                             nullptr, &result, &err_msg);
            if (st == kMpOk) {
                map_norm_landmarks(result.pose_landmarks,
                                   result.pose_landmarks_count, out.pose_landmarks);
                map_world_landmarks(result.pose_world_landmarks,
                                    result.pose_world_landmarks_count,
                                    out.pose_world_landmarks);
                if (result.segmentation_masks_count > 0 && result.segmentation_masks) {
                    for (uint32_t i = 0; i < result.segmentation_masks_count; ++i) {
                        SegmentationMask m;
                        if (map_mask(result.segmentation_masks[i], m)) {
                            out.pose_segmentation_masks.push_back(std::move(m));
                        }
                    }
                }
                MpPoseLandmarkerCloseResult(&result);
                ok = true;
            } else {
                err = "MpPoseLandmarkerDetectImage failed";
            }
            break;
        }
        case MpVisionTask::ObjectDetector: {
            if (!impl.object_detector) break;
            MpObjectDetectorResult result{};
            st = MpObjectDetectorDetectImage(impl.object_detector, mp_img,
                                             nullptr, &result, &err_msg);
            if (st == kMpOk) {
                map_detections(result.detections, result.detections_count, mp_img,
                               out.detections);
                MpObjectDetectorCloseResult(&result);
                ok = true;
            } else {
                err = "MpObjectDetectorDetectImage failed";
            }
            break;
        }
        case MpVisionTask::FaceDetector: {
            if (!impl.face_detector) break;
            MpFaceDetectorResult result{};
            st = MpFaceDetectorDetectImage(impl.face_detector, mp_img, nullptr,
                                           &result, &err_msg);
            if (st == kMpOk) {
                map_detections(result.detections, result.detections_count, mp_img,
                               out.detections);
                MpFaceDetectorCloseResult(&result);
                ok = true;
            } else {
                err = "MpFaceDetectorDetectImage failed";
            }
            break;
        }
        case MpVisionTask::ImageClassifier: {
            if (!impl.image_classifier) break;
            MpImageClassifierResult result{};
            st = MpImageClassifierClassifyImage(impl.image_classifier, mp_img,
                                                nullptr, &result, &err_msg);
            if (st == kMpOk) {
                for (uint32_t i = 0; i < result.classifications_count; ++i) {
                    const ::MpClassifications& head = result.classifications[i];
                    std::vector<Category> cats;
                    if (head.categories && head.categories_count > 0) {
                        cats.reserve(head.categories_count);
                        for (uint32_t j = 0; j < head.categories_count; ++j) {
                            const ::MpCategory& s = head.categories[j];
                            Category c;
                            c.index = s.index;
                            c.score = s.score;
                            if (s.category_name) c.category_name = s.category_name;
                            if (s.display_name) c.display_name = s.display_name;
                            cats.push_back(std::move(c));
                        }
                    }
                    out.classifications.push_back(std::move(cats));
                }
                MpImageClassifierCloseResult(&result);
                ok = true;
            } else {
                err = "MpImageClassifierClassifyImage failed";
            }
            break;
        }
        case MpVisionTask::ImageEmbedder: {
            if (!impl.image_embedder) break;
            ImageEmbedderResult result{};
            st = MpImageEmbedderEmbedImage(impl.image_embedder, mp_img, nullptr,
                                           &result, &err_msg);
            if (st == kMpOk) {
                map_embeddings(result.embeddings, result.embeddings_count,
                               out.embeddings);
                MpImageEmbedderCloseResult(&result);
                ok = true;
            } else {
                err = "MpImageEmbedderEmbedImage failed";
            }
            break;
        }
        case MpVisionTask::ImageSegmenter: {
            if (!impl.image_segmenter) break;
            MpImageSegmenterResult result{};
            st = MpImageSegmenterSegmentImage(impl.image_segmenter, mp_img,
                                              nullptr, &result, &err_msg);
            if (st == kMpOk) {
                if (result.confidence_masks && result.confidence_masks_count > 0) {
                    out.segmentation_masks.reserve(result.confidence_masks_count);
                    for (uint32_t i = 0; i < result.confidence_masks_count; ++i) {
                        SegmentationMask m;
                        if (map_mask(result.confidence_masks[i], m)) {
                            out.segmentation_masks.push_back(std::move(m));
                        }
                    }
                }
                if (result.category_mask) {
                    map_mask_uint8(result.category_mask, out.category_mask);
                }
                MpImageSegmenterCloseResult(&result);
                // 标签（confidence mask 通道的语义名）。
                char* label_err = nullptr;
                MpStringList labels{};
                if (MpImageSegmenterGetLabels(impl.image_segmenter, &labels,
                                              &label_err) == kMpOk) {
                    for (int i = 0; i < labels.num_strings && labels.strings; ++i) {
                        if (labels.strings[i]) {
                            out.segment_labels.emplace_back(labels.strings[i]);
                        }
                    }
                    MpStringListFree(&labels);
                }
                if (label_err) std::free(label_err);
                ok = true;
            } else {
                err = "MpImageSegmenterSegmentImage failed";
            }
            break;
        }
        case MpVisionTask::GestureRecognizer: {
            if (!impl.gesture_recognizer) break;
            MpGestureRecognizerResult result{};
            st = MpGestureRecognizerRecognizeImage(impl.gesture_recognizer,
                                                    mp_img, nullptr, &result,
                                                    &err_msg);
            if (st == kMpOk) {
                map_categories(result.gestures, result.gestures_count, out.gestures);
                map_categories(result.handedness, result.handedness_count,
                               out.handedness);
                map_norm_landmarks(result.hand_landmarks,
                                   result.hand_landmarks_count, out.hand_landmarks);
                map_world_landmarks(result.hand_world_landmarks,
                                    result.hand_world_landmarks_count,
                                    out.hand_world_landmarks);
                MpGestureRecognizerCloseResult(&result);
                ok = true;
            } else {
                err = "MpGestureRecognizerRecognizeImage failed";
            }
            break;
        }
        case MpVisionTask::HolisticLandmarker: {
            if (!impl.holistic_landmarker) break;
            MpHolisticLandmarkerResult result{};
            st = MpHolisticLandmarkerDetectImage(impl.holistic_landmarker, mp_img,
                                                 nullptr, &result, &err_msg);
            if (st == kMpOk) {
                // 单对象结果为单数 struct（无 count）；逐组包成 1 元素向量。
                map_norm_landmarks(&result.face_landmarks, 1, out.face_landmarks);
                map_norm_landmarks(&result.pose_landmarks, 1, out.pose_landmarks);
                map_world_landmarks(&result.pose_world_landmarks, 1,
                                    out.pose_world_landmarks);
                if (result.left_hand_landmarks.landmarks &&
                    result.left_hand_landmarks.landmarks_count > 0) {
                    map_norm_landmarks(&result.left_hand_landmarks, 1,
                                       out.hand_landmarks);
                }
                if (result.right_hand_landmarks.landmarks &&
                    result.right_hand_landmarks.landmarks_count > 0) {
                    map_norm_landmarks(&result.right_hand_landmarks, 1,
                                       out.hand_landmarks);
                }
                if (result.left_hand_world_landmarks.landmarks &&
                    result.left_hand_world_landmarks.landmarks_count > 0) {
                    map_world_landmarks(&result.left_hand_world_landmarks, 1,
                                        out.hand_world_landmarks);
                }
                if (result.right_hand_world_landmarks.landmarks &&
                    result.right_hand_world_landmarks.landmarks_count > 0) {
                    map_world_landmarks(&result.right_hand_world_landmarks, 1,
                                        out.hand_world_landmarks);
                }
                if (result.face_blendshapes.categories &&
                    result.face_blendshapes.categories_count > 0) {
                    map_categories(&result.face_blendshapes, 1, out.face_blendshapes);
                }
                if (result.pose_segmentation_mask) {
                    SegmentationMask m;
                    if (map_mask(result.pose_segmentation_mask, m)) {
                        out.pose_segmentation_masks.push_back(std::move(m));
                    }
                }
                MpHolisticLandmarkerCloseResult(&result);
                ok = true;
            } else {
                err = "MpHolisticLandmarkerDetectImage failed";
            }
            break;
        }
    }

    if (!ok && err.empty()) {
        err = "MediaPipe: task handle not initialized";
    }
    if (!ok) {
        const std::string detail = take_err(err_msg);
        if (!detail.empty()) err += ": " + detail;
    } else if (err_msg) {
        std::free(err_msg);
    }
    MpImageFree(mp_img);
    return ok;
}

}  // namespace task_graph

#else  // !TASK_GRAPH_MEDIAPIPE_AVAILABLE —— stub

namespace task_graph {

// stub 侧空 Impl（unique_ptr 析构需要完整类型）
struct MpVisionEngine::Impl {};

MpVisionEngine::MpVisionEngine() = default;
MpVisionEngine::~MpVisionEngine() = default;

bool MpVisionEngine::gpu_delegate_available() {
    return false;
}

std::shared_ptr<MpVisionEngine> MpVisionEngine::create(MpVisionTask,
                                                       const MpVisionOptions&,
                                                       std::string& err) {
    err = "MediaPipe engine not built — run scripts/build_mediapipe.py then "
          "reconfigure with -DTASK_GRAPH_ENABLE_MEDIAPIPE=ON";
    return nullptr;
}

bool MpVisionEngine::run(const uint8_t*, int, int, int, VisionResult&,
                         std::string& err) {
    err = "MediaPipe engine not built — run scripts/build_mediapipe.py then "
          "reconfigure with -DTASK_GRAPH_ENABLE_MEDIAPIPE=ON";
    return false;
}

}  // namespace task_graph

#endif  // TASK_GRAPH_MEDIAPIPE_AVAILABLE

// Image 便捷重载：两态共用（纯转发）。
namespace task_graph {

bool MpVisionEngine::run(const Image& image, VisionResult& out, std::string& err) {
    if (!image.data) {
        err = "MediaPipe: empty image";
        return false;
    }
    Image copy = image;  // ensure_cpu 需要 non-const
    if (!copy.ensure_cpu() || !copy.valid()) {
        err = "MediaPipe: image not available on CPU";
        return false;
    }
    return run(copy.data->data(), copy.width, copy.height, copy.channels, out, err);
}

}  // namespace task_graph
