#pragma once

#include <vector>
#include <thread>
#include <queue>
#include <atomic>
#include <future>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <memory>

namespace task_graph {

class ThreadPool {
public:
    ThreadPool(size_t num_threads = std::thread::hardware_concurrency());
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<ReturnType> future = task->get_future();

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (stopped_) {
                throw std::runtime_error("ThreadPool has been stopped");
            }
            tasks_.push([task]() { (*task)(); });
        }

        condition_.notify_one();

        return future;
    }

    size_t num_threads() const { return threads_.size(); }
    size_t pending_tasks() const;

private:
    void worker();

    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stopped_{false};
};

using ThreadPoolPtr = std::shared_ptr<ThreadPool>;

}
