#include <plugin_api.hpp>

namespace task_graph {

void TaskParams::set_int(const std::string& key, int value) {
    params_[key] = value;
}

void TaskParams::set_float(const std::string& key, float value) {
    params_[key] = value;
}

void TaskParams::set_string(const std::string& key, const std::string& value) {
    params_[key] = value;
}

void TaskParams::set_bool(const std::string& key, bool value) {
    params_[key] = value;
}

std::optional<int> TaskParams::get_int(const std::string& key) const {
    auto it = params_.find(key);
    if (it == params_.end()) return std::nullopt;
    const int* p = std::get_if<int>(&it->second);
    return p ? std::optional<int>(*p) : std::nullopt;
}

std::optional<float> TaskParams::get_float(const std::string& key) const {
    auto it = params_.find(key);
    if (it == params_.end()) return std::nullopt;
    const float* p = std::get_if<float>(&it->second);
    return p ? std::optional<float>(*p) : std::nullopt;
}

std::optional<std::string> TaskParams::get_string(const std::string& key) const {
    auto it = params_.find(key);
    if (it == params_.end()) return std::nullopt;
    const std::string* p = std::get_if<std::string>(&it->second);
    return p ? std::optional<std::string>(*p) : std::nullopt;
}

std::optional<bool> TaskParams::get_bool(const std::string& key) const {
    auto it = params_.find(key);
    if (it == params_.end()) return std::nullopt;
    const bool* p = std::get_if<bool>(&it->second);
    return p ? std::optional<bool>(*p) : std::nullopt;
}

bool TaskParams::has_param(const std::string& key) const {
    return params_.contains(key);
}

void TaskParams::clear() {
    params_.clear();
}

}
