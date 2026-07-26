#include <plugin_api.hpp>
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <thread>
#include <mutex>
#include <filesystem>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <processthreadsapi.h>
#else
#include <pthread.h>
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
#elif defined(__EMSCRIPTEN__)
        ss << std::this_thread::get_id();
#else
        ss << syscall(SYS_gettid);
#endif
        return ss.str();
    }

    std::string get_thread_name() {
#ifdef _WIN32
        char buffer[256];
        DWORD result = GetThreadDescription(GetCurrentThread());
        if (result != NULL) {
            wcscpy_s(buffer, 256, result);
            LocalFree(result);
            return std::string(buffer);
        }
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

        void log(LogLevel level, const std::string& msg, const char* file, int line) {
            if (!is_enabled(level)) return;

            std::lock_guard<std::mutex> lock(mutex_);
            
            std::stringstream ss;
            ss << "[" << get_timestamp() << "] ";
            ss << "[" << std::setw(5) << get_level_name(level) << "] ";
            
            std::string thread_name = get_thread_name();
            std::string thread_id = get_thread_id();
            if (!thread_name.empty()) {
                ss << "[" << thread_name << "/" << thread_id << "] ";
            } else {
                ss << "[T/" << thread_id << "] ";
            }
            
            std::string filename = get_filename(file);
            if (!filename.empty() && line > 0) {
                ss << "[" << filename << ":" << line << "] ";
            } else if (!filename.empty()) {
                ss << "[" << filename << "] ";
            }
            
            ss << msg << std::endl;

            std::cout << ss.str();
            std::cout.flush();
        }

    private:
        LoggerImpl() {}
        ~LoggerImpl() {}
        LoggerImpl(const LoggerImpl&) = delete;
        LoggerImpl& operator=(const LoggerImpl&) = delete;

        mutable std::mutex mutex_;
        LogLevel level_{LogLevel::INFO};
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

}
