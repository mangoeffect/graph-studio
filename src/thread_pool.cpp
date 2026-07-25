#include <task_graph/thread_pool.hpp>
#include <stdexcept>

namespace task_graph {

ThreadPool::ThreadPool(size_t num_threads) {
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) {
            num_threads = 4;
        }
    }

    for (size_t i = 0; i < num_threads; ++i) {
        threads_.emplace_back(&ThreadPool::worker, this);
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stopped_ = true;
    }
    condition_.notify_all();

    for (std::thread& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void ThreadPool::worker() {
    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            condition_.wait(lock, [this]() {
                return stopped_ || !tasks_.empty();
            });

            if (stopped_ && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }

        task();
    }
}

size_t ThreadPool::pending_tasks() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

}
