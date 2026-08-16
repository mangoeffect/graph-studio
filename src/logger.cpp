#include <plugin_api.hpp>
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <thread>
#include <mutex>
#include <filesystem>
#include <sstream>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <processthreadsapi.h>
// windows.h 会定义宏 ERROR(0)，会破坏下面 LogLevel::ERROR 枚举项
#undef ERROR
#else
#include <pthread.h>
#if defined(__ANDROID__)
#include <unistd.h>
#elif !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
#include <sys/syscall.h>
#include <unistd.h>
#endif
#endif

namespace task_graph {

namespace {
    thread_local std::stringstream thread_buffer_;

    std::string get_filename(const char* path) {
        if (!path || path[0] == '\0') return "";
        return std::filesystem::path(path).filename().string();
    }

    std::string get_thread_id() {
        std::stringstream ss;
#ifdef _WIN32
        ss << GetCurrentThreadId();
#elif __APPLE__
        uint64_t tid;
        pthread_threadid_np(NULL, &tid);
        ss << tid;
#elif defined(__ANDROID__)
        ss << gettid();
#elif defined(__EMSCRIPTEN__)
        ss << std::this_thread::get_id();
#else
        ss << syscall(SYS_gettid);
#endif
        return ss.str();
    }

    std::string get_thread_name() {
#ifdef _WIN32
        // GetThreadDescription(HANDLE, PWSTR*) 仅在 Win10 1607+ 可用；
        // 失败或空描述符时返回空串。
        PWSTR desc = nullptr;
        if (FAILED(GetThreadDescription(GetCurrentThread(), &desc)) || !desc) {
            return "";
        }
        std::string name;
        int len = WideCharToMultiByte(CP_UTF8, 0, desc, -1, nullptr, 0, nullptr, nullptr);
        if (len > 1) {
            name.resize(static_cast<size_t>(len - 1));
            WideCharToMultiByte(CP_UTF8, 0, desc, -1, name.data(), len, nullptr, nullptr);
        }
        LocalFree(desc);
        return name;
#elif defined(__ANDROID__)
        // Android bionic 不提供 pthread_getname_np
        return "";
#elif defined(__EMSCRIPTEN__)
        return "main";
#else
        char buffer[16];
        if (pthread_getname_np(pthread_self(), buffer, sizeof(buffer)) == 0) {
            return std::string(buffer);
        }
        return "";
#endif
    }

    std::string get_level_name(LogLevel level) {
        switch (level) {
            case LogLevel::TRACE: return "TRACE";
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO";
            case LogLevel::WARN:  return "WARN";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::FATAL: return "FATAL";
            default:              return "UNKNOWN";
        }
    }

    std::string get_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        ss << "." << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    class LoggerImpl {
    public:
        static LoggerImpl& instance() {
            static LoggerImpl instance;
            return instance;
        }

        void set_level(LogLevel level) {
            std::lock_guard<std::mutex> lock(mutex_);
            level_ = level;
        }

        LogLevel get_level() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return level_;
        }

        bool is_enabled(LogLevel level) const {
            return level >= level_;
        }

        void set_sink(LogSink sink) {
            std::lock_guard<std::mutex> lock(mutex_);
            sink_ = std::move(sink);
        }

        void clear_sink() {
            std::lock_guard<std::mutex> lock(mutex_);
            sink_ = {};
        }

        LogSinkHandle add_extra_sink(LogSink sink) {
            if (!sink) return 0;
            std::lock_guard<std::mutex> lock(mutex_);
            // 句柄从 1 起；0 保留为"无效"。
            ++next_handle_;
            extra_sinks_.emplace_back(next_handle_, std::move(sink));
            return next_handle_;
        }

        void remove_extra_sink(LogSinkHandle handle) {
            if (handle == 0) return;
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto it = extra_sinks_.begin(); it != extra_sinks_.end(); ++it) {
                if (it->first == handle) {
                    extra_sinks_.erase(it);
                    return;
                }
            }
        }

        void log(LogLevel level, const std::string& msg, const char* file, int line) {
            if (!is_enabled(level)) return;

            LogSink sink_copy;
            std::vector<LogSink> extra_copies;
            LogEntry entry;
            {
                std::lock_guard<std::mutex> lock(mutex_);

                std::string timestamp = get_timestamp();
                std::string thread_name = get_thread_name();
                std::string thread_id = get_thread_id();
                std::string filename = get_filename(file);

                std::stringstream ss;
                ss << "[" << timestamp << "] ";
                ss << "[" << std::setw(5) << get_level_name(level) << "] ";
                if (!thread_name.empty()) {
                    ss << "[" << thread_name << "/" << thread_id << "] ";
                } else {
                    ss << "[T/" << thread_id << "] ";
                }
                if (!filename.empty() && line > 0) {
                    ss << "[" << filename << ":" << line << "] ";
                } else if (!filename.empty()) {
                    ss << "[" << filename << "] ";
                }
                ss << msg << std::endl;

                std::cout << ss.str();
                std::cout.flush();

                entry.level = level;
                entry.msg = msg;
                entry.file = file;
                entry.line = line;
                entry.filename = std::move(filename);
                entry.timestamp = std::move(timestamp);
                entry.thread_name = std::move(thread_name);
                entry.thread_id = std::move(thread_id);

                sink_copy = sink_;
                extra_copies.reserve(extra_sinks_.size());
                for (const auto& kv : extra_sinks_) {
                    extra_copies.push_back(kv.second);
                }
            }
            // 锁外调用 sink：避免 sink 内部再调 tg_log 导致重入死锁。
            // 可能在任意线程触发，调用方需自行 marshal 回目标线程。
            // 顺序：主槽（set_log_sink）先于附加观察者（add_log_sink，按注册序）。
            if (sink_copy) {
                sink_copy(entry);
            }
            for (const auto& s : extra_copies) {
                if (s) s(entry);
            }
        }

    private:
        LoggerImpl() {}
        ~LoggerImpl() {}
        LoggerImpl(const LoggerImpl&) = delete;
        LoggerImpl& operator=(const LoggerImpl&) = delete;

        mutable std::mutex mutex_;
        LogLevel level_{LogLevel::INFO};
        LogSink sink_;
        // 附加观察者 sink：<句柄, sink>，注册序即回调序。
        std::vector<std::pair<LogSinkHandle, LogSink>> extra_sinks_;
        LogSinkHandle next_handle_{0};
    };
}

extern "C" TG_EXPORT void tg_log(LogLevel level, const char* msg, const char* file, int line) {
    LoggerImpl::instance().log(level, msg, file, line);
}

extern "C" TG_EXPORT void tg_set_log_level(LogLevel level) {
    LoggerImpl::instance().set_level(level);
}

extern "C" TG_EXPORT LogLevel tg_get_log_level() {
    return LoggerImpl::instance().get_level();
}

TG_EXPORT void set_log_sink(LogSink sink) {
    LoggerImpl::instance().set_sink(std::move(sink));
}

TG_EXPORT void clear_log_sink() {
    LoggerImpl::instance().clear_sink();
}

TG_EXPORT LogSinkHandle add_log_sink(LogSink sink) {
    return LoggerImpl::instance().add_extra_sink(std::move(sink));
}

TG_EXPORT void remove_log_sink(LogSinkHandle handle) {
    LoggerImpl::instance().remove_extra_sink(handle);
}

}
