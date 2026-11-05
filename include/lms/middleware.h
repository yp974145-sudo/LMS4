#pragma once

#include "lms/http_server.h"
#include "lms/jwt.h"
#include "lms/logger.h"
#include <string>
#include <map>
#include <chrono>
#include <mutex>

namespace lms::middleware {

// ============================================
// JWT 认证中间件
// ============================================
class Auth {
    std::string secret_;
    std::vector<std::string> public_paths_; // 不需要认证的路径

public:
    explicit Auth(const std::string& secret, std::vector<std::string> public_paths = {})
        : secret_(secret), public_paths_(std::move(public_paths)) {}

    // 返回 true = 通过，false = 已设置 401 响应
    bool check(const http::Request& req, http::Response& res, jwt::Claims* out_claims = nullptr) {
        // 公开路径跳过认证
        for (const auto& pp : public_paths_) {
            if (req.path == pp || (pp.size() > 1 && pp.back() == '*' &&
                                   req.path.rfind(pp.substr(0, pp.size() - 1), 0) == 0))
                return true;
        }

        auto auth_hdr = req.get_header("Authorization");
        auto token_opt = jwt::extract_bearer(auth_hdr);
        if (!token_opt) {
            res.status = 401;
            res.set_json(R"({"error":"缺少认证令牌","code":401})");
            return false;
        }

        auto claims = jwt::verify(*token_opt, secret_);
        if (!claims) {
            res.status = 401;
            res.set_json(R"({"error":"令牌无效或已过期","code":401})");
            return false;
        }

        if (out_claims) *out_claims = *claims;
        return true;
    }
};

// ============================================
// 请求日志中间件
// ============================================
class RequestLogger {
public:
    // 包装 handler：记录请求方法、路径、状态码、耗时
    template<typename Handler>
    static auto wrap(Handler&& handler) {
        return [h = std::forward<Handler>(handler)](const http::Request& req, http::Response& res) {
            auto start = std::chrono::steady_clock::now();
            h(req, res);
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();

            LOG_INFO(req.method + " " + req.path + " → " +
                     std::to_string(res.status) + " (" +
                     std::to_string(elapsed) + "ms)");
        };
    }
};

// ============================================
// 限流中间件（Token Bucket）
// ============================================
class RateLimiter {
    struct Bucket {
        double tokens;
        std::chrono::steady_clock::time_point last_refill;
    };

    std::map<std::string, Bucket> buckets_;
    std::mutex mutex_;
    double rate_;       // tokens per second
    double capacity_;   // max tokens

public:
    // 默认：每秒 30 个请求，突发 50
    explicit RateLimiter(double rate = 30.0, double capacity = 50.0)
        : rate_(rate), capacity_(capacity) {}

    // 返回 true = 允许，false = 限流（429）
    bool allow(const std::string& key) {
        std::lock_guard lock(mutex_);
        auto now = std::chrono::steady_clock::now();

        auto& bucket = buckets_[key];
        if (bucket.tokens <= 0 && bucket.last_refill.time_since_epoch().count() == 0) {
            // 新桶，初始满
            bucket.tokens = capacity_;
            bucket.last_refill = now;
        } else {
            // 补充 token
            auto elapsed = std::chrono::duration<double>(now - bucket.last_refill).count();
            bucket.tokens = std::min(capacity_, bucket.tokens + elapsed * rate_);
            bucket.last_refill = now;
        }

        if (bucket.tokens >= 1.0) {
            bucket.tokens -= 1.0;
            return true;
        }
        return false;
    }

    // 中间件包装
    template<typename Handler>
    auto wrap(Handler&& handler) {
        return [this, h = std::forward<Handler>(handler)]
               (const http::Request& req, http::Response& res) {
            // 用客户端 IP 作为限流 key
            std::string ip = req.get_header("X-Forwarded-For");
            if (ip.empty()) ip = "127.0.0.1"; // 简化：从 socket 取 IP 较复杂

            if (!allow(ip)) {
                res.status = 429;
                res.set_json(R"({"error":"请求过于频繁，请稍后再试","code":429})");
                return;
            }
            h(req, res);
        };
    }
};

} // namespace lms::middleware
