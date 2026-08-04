#pragma once

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

struct VisionResult {
    std::vector<std::vector<NormalizedLandmark>> face_landmarks;
    std::vector<std::vector<NormalizedLandmark>> hand_landmarks;
    std::vector<std::vector<NormalizedLandmark>> pose_landmarks;
    std::vector<Detection> detections;
    std::string task_type;
};

}  // namespace task_graph
