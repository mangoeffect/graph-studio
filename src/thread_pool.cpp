#include <task_graph/thread_pool.hpp>
#include <stdexcept>

namespace task_graph {

ThreadPool::ThreadPool(size_t num_threads) {
#if defined(__EMSCRIPTEN__)
    // WASM（单/多线程 build 一致）：退化为 inline 模式（submit 时同步执行），
    // num_threads_ 保持 0，不创建任何 std::thread。
    // - 单线程 build：std::thread 构造直接 abort。
    // - 多线程 build（wasm_multithread）：每会话重建的 ThreadPool 需把 pthread
    //   重宿主到已退出的 Web Worker，而主线程此刻正阻塞在调度等待（主线程
    //   无 Atomics.wait，futex 模拟为忙等，事件循环停转），重宿主永不完成
    //   → 同页第二次执行整页死锁（2026-09-27 在线部署版实测定位）。GPU
    //   任务本就主线程内联（prefer_main_thread），CPU 任务也内联后调度循环
    //   同栈执行，cv 等待条件即时满足、不再真正阻塞。
    (void)num_threads;
    num_threads_ = 0;
    return;
#else
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) {
            num_threads = 4;
        }
    }

    num_threads_ = num_threads;
    for (size_t i = 0; i < num_threads; ++i) {
        threads_.emplace_back(&ThreadPool::worker, this);
    }
#endif
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
