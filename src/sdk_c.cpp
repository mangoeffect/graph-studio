// tg_sdk_c.h 实现:TaskGraphSdk(C++)的纯 C 包装。
// 设计要点:
//  - 句柄内部持有 shared_ptr<TaskGraphSdk> + C 侧专用缓存(字符串返回值
//    生命周期:到下次同族访问器调用或销毁);
//  - std::any 边界只暴露 C 类型子集(int32/double/string/bool/Image);
//  - 异步执行用 future 存储(无线程泄漏;wait 收割)。

#include <task_graph/tg_sdk_c.h>

#include <task_graph/sdk.hpp>
#include <task_graph/data_types.hpp>

#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// ---- 图像句柄:按值持有 Image(像素经 shared_ptr 共享,拷贝廉价) ----
struct tg_image {
    task_graph::Image img;
};

struct tg_sdk {
    std::shared_ptr<task_graph::TaskGraphSdk> impl;

    // C 侧缓存(const char* 返回值的稳定来源)
    mutable std::mutex cache_mutex;
    std::string err_cache;
    std::vector<task_graph::DagConfigIssue> issues_cache;
    std::vector<std::string> in_nodes_cache;
    std::vector<std::string> out_nodes_cache;
    task_graph::GraphDiff diff_cache;

    // 异步执行
    std::mutex fut_mutex;
    std::future<task_graph::SdkStatus> pending;

    void cache_error() {
        std::lock_guard<std::mutex> lock(cache_mutex);
        err_cache = impl->last_error();
    }
};

using task_graph::SdkStatus;

namespace {

// tg_status ↔ SdkStatus(数值一致,双保险转换)
inline int status_code(SdkStatus s) { return static_cast<int>(s); }

// 拷贝式字符串出参:返回含 NUL 的所需长度(同 snprintf 语义)
int copy_str(char* buf, int buf_size, const std::string& src) {
    const int needed = static_cast<int>(src.size()) + 1;
    if (buf && buf_size > 0) {
        const int n = (needed < buf_size) ? needed : buf_size;
        std::memcpy(buf, src.c_str(), static_cast<size_t>(n - 1));
        buf[n - 1] = '\0';
    }
    return needed;
}

}  // namespace

extern "C" {

// ====================== 生命周期 ======================

tg_sdk* tg_sdk_create(void) {
    auto impl = task_graph::TaskGraphSdk::create();
    if (!impl) return nullptr;
    auto* h = new tg_sdk;
    h->impl = std::move(impl);
    return h;
}

int tg_sdk_init(tg_sdk* sdk, const tg_sdk_config* cfg) {
    if (!sdk) return TG_ERR_INVALID_ARGUMENT;
    task_graph::SdkConfig c;
    if (cfg) {
        c.log_level = static_cast<task_graph::LogLevel>(cfg->log_level);
        if (cfg->log_callback) {
            auto fn = cfg->log_callback;
            void* user = cfg->log_user_data;
            c.log_callback = [fn, user](const task_graph::LogEntry& e) {
                fn(user, static_cast<int>(e.level), e.msg.c_str(),
                   e.filename.c_str(), e.line);
            };
        }
        if (cfg->thread_pool_size > 0) c.thread_pool_size = cfg->thread_pool_size;
        c.default_timeout = std::chrono::milliseconds(cfg->default_timeout_ms);
        c.enable_profiling = cfg->enable_profiling != 0;
        c.require_known_types = cfg->require_known_types != 0;
    }
    return status_code(sdk->impl->init(c));
}

int tg_sdk_is_initialized(const tg_sdk* sdk) {
    if (!sdk) return 0;
    return sdk->impl->is_initialized() ? 1 : 0;
}

int tg_sdk_shutdown(tg_sdk* sdk) {
    if (!sdk) return TG_ERR_INVALID_ARGUMENT;
    // 先收割未 wait 的异步执行,避免悬垂 future
    {
        std::lock_guard<std::mutex> lock(sdk->fut_mutex);
        if (sdk->pending.valid()) sdk->pending.wait();
    }
    return status_code(sdk->impl->shutdown());
}

void tg_sdk_destroy(tg_sdk* sdk) {
    if (!sdk) return;
    tg_sdk_shutdown(sdk);
    delete sdk;
}

const char* tg_sdk_last_error(tg_sdk* sdk) {
    if (!sdk) return "";
    std::lock_guard<std::mutex> lock(sdk->cache_mutex);
    sdk->err_cache = sdk->impl->last_error();
    return sdk->err_cache.c_str();
}

// ====================== graph 加载/更新 ======================

int tg_sdk_load_graph_file(tg_sdk* sdk, const char* path) {
    if (!sdk || !path) return TG_ERR_INVALID_ARGUMENT;
    int st = status_code(sdk->impl->load_graph_file(path));
    if (st != TG_OK) sdk->cache_error();
    return st;
}

int tg_sdk_load_graph_json(tg_sdk* sdk, const char* json) {
    if (!sdk || !json) return TG_ERR_INVALID_ARGUMENT;
    int st = status_code(sdk->impl->load_graph_string(json));
    if (st != TG_OK) sdk->cache_error();
    return st;
}

int tg_sdk_update_graph_file(tg_sdk* sdk, const char* path) {
    if (!sdk || !path) return TG_ERR_INVALID_ARGUMENT;
    int st = status_code(sdk->impl->update_graph_file(path));
    if (st != TG_OK) sdk->cache_error();
    return st;
}

int tg_sdk_update_graph_json(tg_sdk* sdk, const char* json) {
    if (!sdk || !json) return TG_ERR_INVALID_ARGUMENT;
    int st = status_code(sdk->impl->update_graph_string(json));
    if (st != TG_OK) sdk->cache_error();
    // 刷新 diff 缓存(update 成功与否都指向最近一次成功应用的 diff;
    // 失败时旧图保留,diff 也保持旧值)
    {
        std::lock_guard<std::mutex> lock(sdk->cache_mutex);
        sdk->diff_cache = sdk->impl->last_diff();
    }
    return st;
}

int tg_sdk_issue_count(tg_sdk* sdk) {
    if (!sdk) return 0;
    std::lock_guard<std::mutex> lock(sdk->cache_mutex);
    sdk->issues_cache = sdk->impl->last_load_issues();
    return static_cast<int>(sdk->issues_cache.size());
}

int tg_sdk_issue(tg_sdk* sdk, int index, int* severity, int* stage,
                 char* pointer_buf, int pointer_buf_size,
                 int64_t* line, int64_t* column,
                 char* message_buf, int message_buf_size) {
    if (!sdk) return TG_ERR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(sdk->cache_mutex);
    if (index < 0 || static_cast<size_t>(index) >= sdk->issues_cache.size()) {
        return TG_ERR_INVALID_ARGUMENT;
    }
    const auto& iss = sdk->issues_cache[index];
    if (severity) *severity = iss.severity == task_graph::DagConfigIssue::Severity::ERROR ? 0 : 1;
    if (stage) {
        *stage = iss.stage == task_graph::DagConfigIssue::Stage::Parse ? 0
               : iss.stage == task_graph::DagConfigIssue::Stage::Schema ? 1
               : iss.stage == task_graph::DagConfigIssue::Stage::Semantics ? 2 : 3;
    }
    if (line) *line = static_cast<int64_t>(iss.line);
    if (column) *column = static_cast<int64_t>(iss.column);
    if (pointer_buf) copy_str(pointer_buf, pointer_buf_size, iss.json_pointer);
    if (message_buf) copy_str(message_buf, message_buf_size, iss.message);
    return TG_OK;
}

int tg_sdk_input_node_count(tg_sdk* sdk) {
    if (!sdk) return 0;
    std::lock_guard<std::mutex> lock(sdk->cache_mutex);
    sdk->in_nodes_cache = sdk->impl->input_nodes();
    return static_cast<int>(sdk->in_nodes_cache.size());
}

const char* tg_sdk_input_node(tg_sdk* sdk, int index) {
    if (!sdk) return nullptr;
    std::lock_guard<std::mutex> lock(sdk->cache_mutex);
    if (index < 0 || static_cast<size_t>(index) >= sdk->in_nodes_cache.size()) {
        return nullptr;
    }
    return sdk->in_nodes_cache[index].c_str();
}

int tg_sdk_output_node_count(tg_sdk* sdk) {
    if (!sdk) return 0;
    std::lock_guard<std::mutex> lock(sdk->cache_mutex);
    sdk->out_nodes_cache = sdk->impl->output_nodes();
    return static_cast<int>(sdk->out_nodes_cache.size());
}

const char* tg_sdk_output_node(tg_sdk* sdk, int index) {
    if (!sdk) return nullptr;
    std::lock_guard<std::mutex> lock(sdk->cache_mutex);
    if (index < 0 || static_cast<size_t>(index) >= sdk->out_nodes_cache.size()) {
        return nullptr;
    }
    return sdk->out_nodes_cache[index].c_str();
}

// ====================== 全局配置 ======================

int tg_sdk_set_env(tg_sdk* sdk, const char* key, const char* value) {
    if (!sdk || !key || !value) return TG_ERR_INVALID_ARGUMENT;
    return status_code(sdk->impl->set_global("_env." + std::string(key),
                                             std::any(std::string(value))));
}

int tg_sdk_set_global_string(tg_sdk* sdk, const char* key, const char* value) {
    if (!sdk || !key || !value) return TG_ERR_INVALID_ARGUMENT;
    return status_code(sdk->impl->set_global(key, std::any(std::string(value))));
}

int tg_sdk_set_global_int(tg_sdk* sdk, const char* key, int32_t value) {
    if (!sdk || !key) return TG_ERR_INVALID_ARGUMENT;
    return status_code(sdk->impl->set_global(key, std::any(value)));
}

int tg_sdk_set_global_double(tg_sdk* sdk, const char* key, double value) {
    if (!sdk || !key) return TG_ERR_INVALID_ARGUMENT;
    return status_code(sdk->impl->set_global(key, std::any(value)));
}

// ====================== 图像句柄 ======================

tg_image* tg_image_create(int width, int height, int channels,
                          int pixel_format, int data_type,
                          const void* data) {
    if (width <= 0 || height <= 0 || channels <= 0) return nullptr;
    auto* img = new tg_image;
    img->img = task_graph::Image(width, height, channels,
                                 static_cast<task_graph::PixelFormat>(pixel_format));
    img->img.data_type = static_cast<task_graph::DataType>(data_type);
    if (data) {
        const size_t bytes = img->img.total_size();
        img->img.data = std::make_shared<std::vector<uint8_t>>(
            static_cast<const uint8_t*>(data),
            static_cast<const uint8_t*>(data) + bytes);
    } else {
        img->img.data = std::make_shared<std::vector<uint8_t>>(img->img.total_size(), 0);
    }
    return img;
}

int tg_image_width(const tg_image* img) { return img ? img->img.width : 0; }
int tg_image_height(const tg_image* img) { return img ? img->img.height : 0; }
int tg_image_channels(const tg_image* img) { return img ? img->img.channels : 0; }
int tg_image_pixel_format(const tg_image* img) {
    return img ? static_cast<int>(img->img.pixel_format) : 0;
}
int tg_image_data_type(const tg_image* img) {
    return img ? static_cast<int>(img->img.data_type) : 0;
}
const uint8_t* tg_image_data(const tg_image* img) {
    if (!img || !img->img.data) return nullptr;
    return img->img.data->data();
}
size_t tg_image_data_size(const tg_image* img) {
    return img ? img->img.total_size() : 0;
}
void tg_image_destroy(tg_image* img) { delete img; }

// ====================== 绑定 ======================

int tg_sdk_bind_input_string(tg_sdk* sdk, const char* node, const char* value) {
    if (!sdk || !node || !value) return TG_ERR_INVALID_ARGUMENT;
    return status_code(sdk->impl->bind_input(node, std::any(std::string(value))));
}

int tg_sdk_bind_input_int(tg_sdk* sdk, const char* node, int32_t value) {
    if (!sdk || !node) return TG_ERR_INVALID_ARGUMENT;
    return status_code(sdk->impl->bind_input(node, std::any(value)));
}

int tg_sdk_bind_input_double(tg_sdk* sdk, const char* node, double value) {
    if (!sdk || !node) return TG_ERR_INVALID_ARGUMENT;
    return status_code(sdk->impl->bind_input(node, std::any(value)));
}

int tg_sdk_bind_input_bool(tg_sdk* sdk, const char* node, int value) {
    if (!sdk || !node) return TG_ERR_INVALID_ARGUMENT;
    return status_code(sdk->impl->bind_input(node, std::any(value != 0)));
}

int tg_sdk_bind_input_image(tg_sdk* sdk, const char* node, const tg_image* img) {
    if (!sdk || !node || !img) return TG_ERR_INVALID_ARGUMENT;
    // Image 拷贝共享像素缓冲(shared_ptr),零拷贝
    return status_code(sdk->impl->bind_input(node, std::any(img->img)));
}

int tg_sdk_unbind_input(tg_sdk* sdk, const char* node) {
    if (!sdk || !node) return TG_ERR_INVALID_ARGUMENT;
    return status_code(sdk->impl->unbind_input(node));
}

int tg_sdk_bind_output(tg_sdk* sdk, const char* node,
                       tg_output_fn callback, void* user_data) {
    if (!sdk || !node) return TG_ERR_INVALID_ARGUMENT;
    if (!callback) {
        return status_code(sdk->impl->bind_output(node));  // 拉模式
    }
    return status_code(sdk->impl->bind_output(
        node, [callback, user_data](const std::string& id, const std::any&) {
            callback(user_data, id.c_str());
        }));
}

int tg_sdk_unbind_output(tg_sdk* sdk, const char* node) {
    if (!sdk || !node) return TG_ERR_INVALID_ARGUMENT;
    return status_code(sdk->impl->unbind_output(node));
}

// ---- 输出拉取 ----

int tg_sdk_get_output_string(tg_sdk* sdk, const char* node,
                             char* buf, int buf_size) {
    if (!sdk || !node) return -TG_ERR_INVALID_ARGUMENT;
    auto v = sdk->impl->get_output<std::string>(node);
    if (!v.has_value()) return -TG_ERR_INTERNAL;
    return copy_str(buf, buf_size, *v);
}

int tg_sdk_get_output_int(tg_sdk* sdk, const char* node, int32_t* out) {
    if (!sdk || !node || !out) return TG_ERR_INVALID_ARGUMENT;
    auto v = sdk->impl->get_output<int32_t>(node);
    if (!v.has_value()) return TG_ERR_TYPE_MISMATCH;
    *out = *v;
    return TG_OK;
}

int tg_sdk_get_output_double(tg_sdk* sdk, const char* node, double* out) {
    if (!sdk || !node || !out) return TG_ERR_INVALID_ARGUMENT;
    auto v = sdk->impl->get_output<double>(node);
    if (!v.has_value()) return TG_ERR_TYPE_MISMATCH;
    *out = *v;
    return TG_OK;
}

int tg_sdk_get_output_image(tg_sdk* sdk, const char* node, tg_image** out) {
    if (!sdk || !node || !out) return TG_ERR_INVALID_ARGUMENT;
    auto v = sdk->impl->get_output<task_graph::Image>(node);
    if (!v.has_value()) return TG_ERR_TYPE_MISMATCH;
    if (v->is_on_gpu() && !v->ensure_cpu()) {
        return TG_ERR_INTERNAL;  // GPU 驻留且无法下载
    }
    auto* img = new tg_image;
    img->img = std::move(*v);  // 共享像素缓冲
    *out = img;
    return TG_OK;
}

// ====================== 执行 ======================

int tg_sdk_execute(tg_sdk* sdk) {
    if (!sdk) return TG_ERR_INVALID_ARGUMENT;
    int st = status_code(sdk->impl->execute());
    if (st != TG_OK) sdk->cache_error();
    return st;
}

int tg_sdk_execute_async(tg_sdk* sdk) {
    if (!sdk) return TG_ERR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(sdk->fut_mutex);
    if (sdk->pending.valid()) return TG_ERR_BUSY;
    sdk->pending = sdk->impl->execute_async();
    return TG_OK;
}

int tg_sdk_execute_wait(tg_sdk* sdk, int* status_out) {
    if (!sdk) return TG_ERR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(sdk->fut_mutex);
    if (!sdk->pending.valid()) return TG_ERR_INVALID_ARGUMENT;
    SdkStatus st = sdk->pending.get();  // 阻塞并收割
    if (status_out) *status_out = status_code(st);
    return TG_OK;
}

int tg_sdk_is_running(tg_sdk* sdk) {
    if (!sdk) return 0;
    return sdk->impl->is_running() ? 1 : 0;
}

int tg_sdk_cancel(tg_sdk* sdk) {
    if (!sdk) return TG_ERR_INVALID_ARGUMENT;
    return status_code(sdk->impl->cancel());
}

// ====================== diff 报告 ======================

int tg_sdk_diff_count(tg_sdk* sdk, int kind) {
    if (!sdk) return 0;
    std::lock_guard<std::mutex> lock(sdk->cache_mutex);
    const auto& d = sdk->impl->last_diff();
    switch (kind) {
        case 0: return static_cast<int>(d.tasks_added.size());
        case 1: return static_cast<int>(d.tasks_removed.size());
        case 2: return static_cast<int>(d.tasks_updated.size());
        case 3: return static_cast<int>(d.edges_added.size());
        case 4: return static_cast<int>(d.edges_removed.size());
        default: return 0;
    }
}

const char* tg_sdk_diff_item(tg_sdk* sdk, int kind, int index) {
    if (!sdk) return nullptr;
    // 拷进缓存:返回指针到下次同类访问或销毁前稳定
    std::lock_guard<std::mutex> lock(sdk->cache_mutex);
    sdk->diff_cache = sdk->impl->last_diff();
    const std::vector<std::string>* list = nullptr;
    switch (kind) {
        case 0: list = &sdk->diff_cache.tasks_added; break;
        case 1: list = &sdk->diff_cache.tasks_removed; break;
        case 2: list = &sdk->diff_cache.tasks_updated; break;
        case 3: list = &sdk->diff_cache.edges_added; break;
        case 4: list = &sdk->diff_cache.edges_removed; break;
        default: return nullptr;
    }
    if (index < 0 || static_cast<size_t>(index) >= list->size()) return nullptr;
    return (*list)[index].c_str();
}

int tg_sdk_diff_topology_changed(tg_sdk* sdk) {
    if (!sdk) return 0;
    return sdk->impl->last_diff().topology_changed() ? 1 : 0;
}

}  // extern "C"
