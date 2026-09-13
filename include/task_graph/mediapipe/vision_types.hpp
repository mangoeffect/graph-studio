#pragma once

// MediaPipe Vision 结果类型（主框架公共类型，namespace task_graph）。
// 来源：原 submodules/mediapipe/mediapipe_vision 插件，随引擎集成迁入核心库。
// VisionResult 是全部 mp_* 任务的统一输出口载荷。

#include <cstdint>
#include <string>
#include <vector>

#include <plugin_api.hpp>

namespace task_graph {

struct NormalizedLandmark {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float visibility{0.0f};
    float presence{0.0f};
    std::string name;
};

struct Landmark {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float visibility{0.0f};
    float presence{0.0f};
    std::string name;
};

struct Detection {
    std::vector<std::string> labels;
    std::vector<float> scores;
    std::vector<int> indices;
    float x_min{0.0f};
    float y_min{0.0f};
    float x_max{0.0f};
    float y_max{0.0f};
    std::string category_name;
};

// Mirrors MediaPipe C `Category`: handedness (Left/Right) and face blendshapes.
struct Category {
    int index{0};
    float score{0.0f};
    std::string category_name;
    std::string display_name;
};

// Mirrors MediaPipe C `Matrix` (column-major): face transformation matrixes.
struct MatrixData {
    uint32_t rows{0};
    uint32_t cols{0};
    std::vector<float> data;  // column-major
};

// Pose segmentation mask: single-channel float32, value in [0,1].
// (Not task_graph::Image, which is uint8.)
struct SegmentationMask {
    int width{0};
    int height{0};
    std::vector<float> data;
};

// ImageSegmenter optional uint8 category mask (GRAY8): each pixel holds the
// class index it was predicted to belong to.
struct SegmentationCategoryMask {
    int width{0};
    int height{0};
    std::vector<uint8_t> data;
};

// ImageEmbedder result for one embedder head (float128 vector, not quantized).
struct EmbeddingValue {
    int head_index{0};
    std::vector<float> values;
};

struct VisionResult {
    std::vector<std::vector<NormalizedLandmark>> face_landmarks;
    std::vector<std::vector<NormalizedLandmark>> hand_landmarks;
    std::vector<std::vector<NormalizedLandmark>> pose_landmarks;
    std::vector<Detection> detections;
    // Face optional outputs
    std::vector<std::vector<Category>> face_blendshapes;
    std::vector<MatrixData> face_transformation_matrixes;
    // Hand extra outputs
    std::vector<std::vector<Landmark>> hand_world_landmarks;
    std::vector<std::vector<Category>> handedness;
    // Pose extra outputs
    std::vector<std::vector<Landmark>> pose_world_landmarks;
    std::vector<SegmentationMask> pose_segmentation_masks;
    // ImageClassifier: one entry (list of Category per head) per classifier head
    std::vector<std::vector<Category>> classifications;
    // GestureRecognizer: recognized gesture categories per detected hand
    std::vector<std::vector<Category>> gestures;
    // ImageEmbedder: float embedding per head
    std::vector<EmbeddingValue> embeddings;
    // ImageSegmenter: confidence masks + optional uint8 category mask + labels
    std::vector<SegmentationMask> segmentation_masks;
    SegmentationCategoryMask category_mask;
    std::vector<std::string> segment_labels;
    std::string task_type;
};

}  // namespace task_graph
