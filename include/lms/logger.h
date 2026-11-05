#pragma once

#include "lms/common.h"
#include <string>
#include <mutex>
#include <fstream>
#include <iostream>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace lms {

// ========== 轻量日志 ==========
class Logger {
public:
    enum Level { DEBUG, INFO, WARN, ErrorLevel };

    static Logger& instance() {
        static Logger log;
        return log;
    }

    void set_level(Level l) { level_ = l; }
    void set_file(const std::string& path) {
        std::lock_guard lock(mutex_);
        if (file_.is_open()) file_.close();
        file_.open(path, std::ios::app);
    }

    void log(Level lv, const std::string& msg) {
        if (lv < level_) return;

        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::ostringstream line;
        auto local = local_time(t);
        line << std::put_time(&local, "%Y-%m-%d %H:%M:%S")
             << '.' << std::setfill('0') << std::setw(3) << ms.count()
             << " [" << level_str(lv) << "] " << msg;

        std::lock_guard lock(mutex_);
        std::cout << line.str() << std::endl;
        if (file_.is_open()) file_ << line.str() << std::endl;
    }

    void debug(const std::string& m) { log(DEBUG, m); }
    void info(const std::string& m)  { log(INFO, m);  }
    void warn(const std::string& m)  { log(WARN, m);  }
    void error(const std::string& m) { log(ErrorLevel, m); }

private:
    Logger() = default;
    Level level_ = INFO;
    std::mutex mutex_;
    std::ofstream file_;

    static const char* level_str(Level l) {
        switch (l) {
            case DEBUG: return "DEBUG";
            case INFO:  return "INFO ";
            case WARN:  return "WARN ";
            case ErrorLevel: return "ErrorLevel";
        }
        return "?????";
    }
};

// 便捷宏
#define LOG_DEBUG(m) lms::Logger::instance().debug(m)
#define LOG_INFO(m)  lms::Logger::instance().info(m)
#define LOG_WARN(m)  lms::Logger::instance().warn(m)
#define LOG_ERROR(m) lms::Logger::instance().error(m)

} // namespace lms
