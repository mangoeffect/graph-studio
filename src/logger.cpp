#include <plugin_api.hpp>
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <thread>
#include <mutex>
#include <filesystem>

namespace task_graph {

namespace {
    thread_local std::stringstream thread_buffer_;

    std::string get_filename(const char* path) {
        if (!path || path[0] == '\0') return "";
        return std::filesystem::path(path).filename().string();
    }
}

Logger::Logger() {}

Logger::~Logger() {}

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

void Logger::set_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

LogLevel Logger::get_level() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

bool Logger::is_enabled(LogLevel level) const {
    return level >= level_;
}

std::string Logger::get_level_name(LogLevel level) const {
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

std::string Logger::get_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

void Logger::log(LogLevel level, const std::string& msg, const char* file, int line) {
    if (!is_enabled(level)) return;

    std::lock_guard<std::mutex> lock(mutex_);
    
    std::stringstream ss;
    ss << "[" << get_timestamp() << "] ";
    ss << "[" << std::setw(5) << get_level_name(level) << "] ";
    
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

void Logger::trace(const std::string& msg, const char* file, int line) {
    log(LogLevel::TRACE, msg, file, line);
}

void Logger::debug(const std::string& msg, const char* file, int line) {
    log(LogLevel::DEBUG, msg, file, line);
}

void Logger::info(const std::string& msg, const char* file, int line) {
    log(LogLevel::INFO, msg, file, line);
}

void Logger::warn(const std::string& msg, const char* file, int line) {
    log(LogLevel::WARN, msg, file, line);
}

void Logger::error(const std::string& msg, const char* file, int line) {
    log(LogLevel::ERROR, msg, file, line);
}

void Logger::fatal(const std::string& msg, const char* file, int line) {
    log(LogLevel::FATAL, msg, file, line);
}

}
