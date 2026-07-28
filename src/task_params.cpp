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

// 注意：在 WASM 默认 build（-fno-exceptions）下，std::any_cast 类型不符会 abort
// 而非抛 bad_any_cast。因此这里改用 type_info 先匹配类型再 cast，避免依赖异常。
// 桌面平台行为完全等价。

std::optional<int> TaskParams::get_int(const std::string& key) const {
    auto it = params_.find(key);
    if (it == params_.end() || it->second.type() != typeid(int)) {
        return std::nullopt;
    }
    return std::any_cast<int>(it->second);
}

std::optional<float> TaskParams::get_float(const std::string& key) const {
    auto it = params_.find(key);
    if (it == params_.end() || it->second.type() != typeid(float)) {
        return std::nullopt;
    }
    return std::any_cast<float>(it->second);
}

std::optional<std::string> TaskParams::get_string(const std::string& key) const {
    auto it = params_.find(key);
    if (it == params_.end() || it->second.type() != typeid(std::string)) {
        return std::nullopt;
    }
    return std::any_cast<std::string>(it->second);
}

std::optional<bool> TaskParams::get_bool(const std::string& key) const {
    auto it = params_.find(key);
    if (it == params_.end() || it->second.type() != typeid(bool)) {
        return std::nullopt;
    }
    return std::any_cast<bool>(it->second);
}

bool TaskParams::has_param(const std::string& key) const {
    return params_.contains(key);
}

void TaskParams::clear() {
    params_.clear();
}

}
