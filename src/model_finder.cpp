// model_finder.cpp — 全局模型查找服务（ModelFinder 单槽）。
// 宿主在 SDK 初始化时经 set_model_finder() 注入"模型名 → 绝对路径"回调，
// 插件任务侧经 find_model() 查询：空串 = 未命中，调用方回退自身默认路径解析。
// 线程模型与 logger.cpp 的 sink 槽一致：锁内只做拷贝、回调在锁外执行，
// 因此并发查询安全、回调内重入（如再调 tg_log）不会死锁。
#include <plugin_api.hpp>

#include <mutex>
#include <utility>

namespace task_graph {

namespace {

class ModelFinderImpl {
public:
    static ModelFinderImpl& instance() {
        static ModelFinderImpl instance;
        return instance;
    }

    void set_finder(ModelFinder finder) {
        std::lock_guard<std::mutex> lock(mutex_);
        finder_ = std::move(finder);
    }

    void clear_finder() {
        std::lock_guard<std::mutex> lock(mutex_);
        finder_ = {};
    }

    std::string find(const std::string& name) const {
        ModelFinder copy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            copy = finder_;
        }
        // 锁外调用：回调可能重入本服务或调 tg_log；契约要求不得抛异常
        //（-fno-exceptions 构建下异常无法跨 SO 传播）。
        if (!copy) return std::string();
        return copy(name);
    }

private:
    ModelFinderImpl() {}
    ~ModelFinderImpl() {}
    ModelFinderImpl(const ModelFinderImpl&) = delete;
    ModelFinderImpl& operator=(const ModelFinderImpl&) = delete;

    mutable std::mutex mutex_;
    ModelFinder finder_;
};

}  // namespace

TG_EXPORT void set_model_finder(ModelFinder finder) {
    ModelFinderImpl::instance().set_finder(std::move(finder));
}

TG_EXPORT void clear_model_finder() {
    ModelFinderImpl::instance().clear_finder();
}

TG_EXPORT std::string find_model(const std::string& name) {
    return ModelFinderImpl::instance().find(name);
}

}  // namespace task_graph
