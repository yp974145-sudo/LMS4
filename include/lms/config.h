#pragma once

#include "lms/json.h"
#include <string>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <stdexcept>
#include <limits>

namespace lms {

// ========== 配置 ==========
struct AppConfig {
    // 服务器
    int port = 8080;
    int thread_count = 4;
    std::string bind_address = "127.0.0.1";
    std::string data_directory = ".";

    // MySQL
    struct {
        bool enabled = false;
        std::string host = "127.0.0.1";
        int port = 3306;
        std::string user = "root";
        std::string password;
        std::string database = "lms_db";
        int pool_size = 8;
    } mysql;

    // 日志
    struct {
        std::string file = "lms.log";
        std::string level = "info";
    } log;

    static AppConfig load(const std::string& path = "config/config.json") {
        AppConfig cfg;
        std::ifstream f(path);
        if (!f.is_open()) {
            // 文件不存在，用默认配置
            return cfg;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        try {
            auto j = Json::parse(ss.str());

            if (!j.is_object()) throw std::invalid_argument("Expected config object");
            if (j.contains("port")) cfg.port = config_int(j["port"]);
            if (j.contains("thread_count")) cfg.thread_count = config_int(j["thread_count"]);
            if (j.contains("bind_address")) cfg.bind_address = j["bind_address"].as_string();
            if (j.contains("data_directory")) cfg.data_directory = j["data_directory"].as_string();

            if (j.contains("mysql")) {
                auto& m = j["mysql"];
                if (!m.is_object()) throw std::invalid_argument("Expected mysql object");
                if (m.contains("enabled")) cfg.mysql.enabled = m["enabled"].as_bool();
                if (m.contains("host")) cfg.mysql.host = m["host"].as_string();
                if (m.contains("port")) cfg.mysql.port = config_int(m["port"]);
                if (m.contains("user")) cfg.mysql.user = m["user"].as_string();
                if (m.contains("password")) cfg.mysql.password = m["password"].as_string();
                if (m.contains("database")) cfg.mysql.database = m["database"].as_string();
                if (m.contains("pool_size")) cfg.mysql.pool_size = config_int(m["pool_size"]);
            }

            if (j.contains("log")) {
                auto& l = j["log"];
                if (!l.is_object()) throw std::invalid_argument("Expected log object");
                if (l.contains("file")) cfg.log.file = l["file"].as_string();
                if (l.contains("level")) cfg.log.level = l["level"].as_string();
            }
        } catch (const std::exception&) {
            throw std::runtime_error("Invalid configuration file: " + path);
        }
        return cfg;
    }

    // Apply after loading the file; command-line options are applied last.
    void apply_environment() {
        auto number = [](const char* name, int& target) {
            if (const char* value = std::getenv(name)) target = parse_int(value, name);
        };
        auto text = [](const char* name, std::string& target) {
            if (const char* value = std::getenv(name)) target = value;
        };
        number("LMS_PORT", port);
        number("LMS_THREAD_COUNT", thread_count);
        text("LMS_BIND_ADDRESS", bind_address);
        text("LMS_DATA_DIRECTORY", data_directory);
        text("LMS_MYSQL_HOST", mysql.host);
        text("LMS_MYSQL_USER", mysql.user);
        text("LMS_MYSQL_PASSWORD", mysql.password);
        text("LMS_MYSQL_DATABASE", mysql.database);
        number("LMS_MYSQL_PORT", mysql.port);
        number("LMS_MYSQL_POOL_SIZE", mysql.pool_size);
        if (const char* value = std::getenv("LMS_MYSQL_ENABLED")) {
            const std::string flag(value);
            if (flag == "true" || flag == "1") mysql.enabled = true;
            else if (flag == "false" || flag == "0") mysql.enabled = false;
            else throw std::invalid_argument("Invalid LMS_MYSQL_ENABLED");
        }
    }

    void validate() const {
        if (port < 1 || port > 65535 || thread_count < 1 || thread_count > 128 ||
            mysql.port < 1 || mysql.port > 65535 || mysql.pool_size < 1 || mysql.pool_size > 128)
            throw std::invalid_argument("Configuration number out of range");
        if (data_directory.empty()) throw std::invalid_argument("Empty data_directory");
        if (bind_address != "127.0.0.1" && bind_address != "0.0.0.0")
            throw std::invalid_argument("bind_address must be 127.0.0.1 or 0.0.0.0");
    }

    static int parse_int(const std::string& value, const std::string& name) {
        if (value.empty() || value.size() > 10 || value.find_first_not_of("0123456789") != std::string::npos)
            throw std::invalid_argument("Invalid integer: " + name);
        const auto n = std::stoll(value);
        if (n > std::numeric_limits<int>::max()) throw std::invalid_argument("Integer out of range: " + name);
        return static_cast<int>(n);
    }

private:
    static int config_int(const Json& value) {
        if (!value.is_int() || value.as_int() < 0 || value.as_int() > std::numeric_limits<int>::max())
            throw std::invalid_argument("Invalid integer in configuration");
        return static_cast<int>(value.as_int());
    }
};

} // namespace lms
