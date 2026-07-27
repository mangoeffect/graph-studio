#include <plugin_api.hpp>
#include <stdexcept>

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
    if (it != params_.end()) {
        try {
            return std::any_cast<int>(it->second);
        } catch (const std::bad_any_cast&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<float> TaskParams::get_float(const std::string& key) const {
    auto it = params_.find(key);
    if (it != params_.end()) {
        try {
            return std::any_cast<float>(it->second);
        } catch (const std::bad_any_cast&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::string> TaskParams::get_string(const std::string& key) const {
    auto it = params_.find(key);
    if (it != params_.end()) {
        try {
            return std::any_cast<std::string>(it->second);
        } catch (const std::bad_any_cast&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<bool> TaskParams::get_bool(const std::string& key) const {
    auto it = params_.find(key);
    if (it != params_.end()) {
        try {
            return std::any_cast<bool>(it->second);
        } catch (const std::bad_any_cast&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

bool TaskParams::has_param(const std::string& key) const {
    return params_.contains(key);
}

void TaskParams::clear() {
    params_.clear();
}

}