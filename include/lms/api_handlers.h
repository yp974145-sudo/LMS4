#pragma once

#include "lms/common.h"
#include "lms/file_storage.h"
#include "lms/models.h"
#include "lms/order_revision.h"
#include "lms/service.h"
#include "lms/json.h"
#include "lms/http_server.h"
#include "lms/logger.h"
#include "lms/jwt.h"
#include "lms/middleware.h"
#include <memory>

namespace lms::api {

// ========== 辅助函数 ==========
inline Json json_user(const User& u) {
    return Json::make_object({
        {"id",       u.id},
        {"username", u.username},
        {"role",     std::string(role_name(u.role))},
        {"role_id",  static_cast<int>(u.role)},
        {"status",   u.status == UserStatus::Active ? "active" : "locked"}
    });
}

inline Json json_order(const Order& o) {
    return Json::make_object({
        {"order_id",         o.order_id},
        {"edit_token",       order_revision(o)},
        {"customer_name",    o.customer_name},
        {"customer_phone",   o.customer_phone},
        {"customer_address", o.customer_address},
        {"cargo_name",       o.cargo_name},
        {"cargo_type",       std::string(cargo_name(o.cargo_type))},
        {"cargo_type_id",    static_cast<int>(o.cargo_type)},
        {"cargo_weight",     o.cargo_weight},
        {"cargo_quantity",   o.cargo_quantity},
        {"cargo_volume",     o.cargo_volume},
        {"sender_address",   o.sender_address},
        {"receiver_address", o.receiver_address},
        {"status",           std::string(status_name(o.status))},
        {"status_id",        static_cast<int>(o.status)},
        {"reject_reason",    o.reject_reason},
        {"creator",          o.creator},
        {"create_time",      o.create_time},
        {"auditor",          o.auditor},
        {"audit_time",       o.audit_time}
    });
}

inline Json paginated(const std::vector<Json>& items, int total, int page, int total_pages) {
    Json result = Json::make_object({
        {"total_count", total},
        {"current_page", page},
        {"total_pages", total_pages}
    });
    auto arr = Json::make_array({});
    for (auto& it : items) arr.push_back(it);
    result["items"] = arr;
    return result;
}

inline Json error_response(const std::string& msg, int code = 400) {
    return Json::make_object({{"error", msg}, {"code", code}});
}

inline Json ok_response(const std::string& msg = "ok") {
    return Json::make_object({{"message", msg}});
}

inline int bounded_query_int(const http::Request& req, const std::string& key,
                             int fallback, int maximum) {
    auto it = req.query_params.find(key);
    if (it == req.query_params.end()) return fallback;
    const auto& value = it->second;
    if (value.empty() || value.size() > 9 ||
        !std::all_of(value.begin(), value.end(), [](unsigned char c){ return c >= '0' && c <= '9'; }))
        throw std::invalid_argument("Invalid query parameter: " + key);
    int n = std::stoi(value);
    if (n < 1 || n > maximum) throw std::invalid_argument("Query parameter out of range: " + key);
    return n;
}

// Check the original integer before narrowing to uint8_t or int.
inline int bounded_json_int(const Json& value, int minimum, int maximum) {
    if (!value.is_int()) throw std::invalid_argument("Expected integer");
    const auto n = value.as_int();
    if (n < minimum || n > maximum) throw std::invalid_argument("Integer out of range");
    return static_cast<int>(n);
}

inline OrderStatus status_filter(const std::string& value) {
    if (value.empty() || value.size() > 10 || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("Invalid status");
    const auto n = std::stoll(value);
    if (n > 6) throw std::invalid_argument("Invalid status");
    return static_cast<OrderStatus>(n);
}

// ========== 注册所有路由 ==========
inline void register_routes(http::Router& router,
                             UserService& user_svc,
                             OrderService& order_svc,
                             const std::string& jwt_secret,
                             bool using_mysql = false)
{
    // 认证中间件：公开路径跳过检查
    auto auth = std::make_shared<middleware::Auth>(jwt_secret);
    // Value capture extends auth lifetime; user_svc is owned by main.
    auto authorize = [auth, &user_svc](const http::Request& req,
            http::Response& res, std::initializer_list<UserRole> roles,
            jwt::Claims& claims) {
        if (!auth->check(req, res, &claims)) return false;
        auto user = user_svc.current_user(claims.subject);
        if (!user) {
            res.status = 401;
            res.set_json(error_response("Account is inactive", 401).dump());
            return false;
        }
        claims.role = static_cast<int>(user->role); // Current DB role, not stale JWT role.
        if (user->role == UserRole::Admin || roles.size() == 0 ||
            std::find(roles.begin(), roles.end(), user->role) != roles.end()) return true;
        res.status = 403;
        res.set_json(error_response("Permission denied", 403).dump());
        return false;
    };

    // ---- 首页（公开，返回管理后台页面） ----
    router.serve_static("/", "static/index.html");
    router.serve_static("/index.html", "static/index.html");

    // ---- 健康检查（公开） ----
    router.get("/api/health", [](const http::Request& req, http::Response& res) {
        res.set_json(ok_response("LMS v4.0 running").dump());
    });

    // ---- 系统状态（公开，含数据库后端信息） ----
    router.get("/api/status", [using_mysql](const http::Request& req, http::Response& res) {
        res.set_json(Json::make_object({
            {"service", "LMS v4.0"},
            {"version", "4.0.0"},
            {"status", "running"},
            {"auth", "JWT HS256"},
            {"backend", using_mysql ? "MySQL (Connection Pool)" : "File Storage"},
            {"endpoints", Json::make_array({
                "GET  /            管理后台页面",
                "GET  /api/status  系统状态",
                "GET  /api/health  健康检查",
                "POST /api/auth/login",
                "GET  /api/users",
                "POST /api/users",
                "GET  /api/orders",
                "POST /api/orders",
                "POST /api/orders/:id/approve",
                "POST /api/orders/:id/reject"
            })}
        }).dump());
    });

    router.get("/api/auth/me", [authorize, &user_svc](const http::Request& req, http::Response& res) {
        jwt::Claims actor;
        if (!authorize(req,res,{},actor)) return;
        auto user=user_svc.current_user(actor.subject);
        if (!user) { res.status=401;res.set_json(error_response("Account is inactive",401).dump());return; }
        res.set_json(Json::make_object({{"user",json_user(*user)}}).dump());
    });

    // ---- 认证：登录 → 返回 JWT（公开） ----
    router.post("/api/auth/login", [&user_svc, &order_svc, jwt_secret, authorize](const http::Request& req, http::Response& res) {
        try {
            auto j = Json::parse(req.body);
            auto username = j["username"].as_string();
            auto password = j["password"].as_string();

            auto user = user_svc.login(username, password);
            if (!user) {
                res.status = 401;
                res.set_json(error_response("用户名或密码错误", 401).dump());
                return;
            }

            // 签发 JWT（含用户名和角色）
            auto token = jwt::create(
                {user->username, static_cast<int>(user->role)}, jwt_secret);

            auto resp = Json::make_object({
                {"token",     token},
                {"token_type", "Bearer"},
                {"expires_in", 86400},
                {"user",      json_user(*user)}
            });
            res.set_json(resp.dump());
            LOG_INFO("登录成功: " + username);
        } catch (const RepositoryError&) {
            throw; // Storage failures are server errors, never malformed client input.
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_json(error_response(std::string("请求解析失败: ") + e.what()).dump());
        }
    });

    // ======== 以下路由需要认证 ========

    // ---- 用户列表（需认证） ----
    router.get("/api/users", [&user_svc, &order_svc, jwt_secret, authorize](const http::Request& req, http::Response& res) {
        jwt::Claims actor;
        if (!authorize(req, res, {UserRole::Admin}, actor)) return;

        int page = bounded_query_int(req, "page", 1, 1000000);
        int page_size = bounded_query_int(req, "page_size", 20, 100);
        std::string keyword = req.query_params.count("keyword") ? req.query_params.at("keyword") : "";

        auto result = user_svc.list_users(page, page_size, keyword);
        std::vector<Json> items;
        for (auto& u : result.items) items.push_back(json_user(u));
        res.set_json(paginated(items, result.total_count, result.current_page, result.total_pages).dump());
    });

    // ---- 创建用户（需认证） ----
    router.post("/api/users", [&user_svc, &order_svc, jwt_secret, authorize](const http::Request& req, http::Response& res) {
        jwt::Claims actor;
        if (!authorize(req, res, {UserRole::Admin}, actor)) return;

        try {
            auto j = Json::parse(req.body);
            auto username = j["username"].as_string();
            auto password = j["password"].as_string();
            auto role     = static_cast<UserRole>(bounded_json_int(j["role"], 0, 3));

            auto result = user_svc.create_user(username, password, password, role);
            if (!result.ok) {
                res.status = 409;
                res.set_json(error_response(result.error, 409).dump());
                return;
            }
            res.status = 201;
            res.set_json(Json::make_object({
                {"message", "创建成功"},
                {"user", json_user(result.user)}
            }).dump());
            LOG_INFO("创建用户: " + username);
        } catch (const RepositoryError&) {
            throw; // Storage failures are server errors, never malformed client input.
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_json(error_response(std::string("请求解析失败: ") + e.what()).dump());
        }
    });

    // ---- 重置密码（需认证） ----
    router.post("/api/users/:id/reset-password", [&user_svc, &order_svc, jwt_secret, authorize](const http::Request& req, http::Response& res) {
        jwt::Claims actor;
        if (!authorize(req, res, {UserRole::Admin}, actor)) return;

        try {
            auto j = Json::parse(req.body);
            std::string target = j["username"].as_string();
            if (user_svc.reset_password(target)) {
                res.set_json(ok_response("密码已重置为 888888").dump());
                LOG_INFO("密码重置: " + target);
            } else {
                res.status = 404;
                res.set_json(error_response("用户不存在", 404).dump());
            }
        } catch (const RepositoryError&) {
            throw; // Storage failures are server errors, never malformed client input.
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_json(error_response(e.what()).dump());
        }
    });

    // ---- 创建订单（需认证） ----
    router.post("/api/orders", [&user_svc, &order_svc, jwt_secret, authorize](const http::Request& req, http::Response& res) {
        jwt::Claims actor;
        if (!authorize(req, res, {UserRole::CustomerSvc}, actor)) return;

        try {
            auto j = Json::parse(req.body);
            Order o;
            o.customer_name    = j["customer_name"].as_string();
            o.customer_phone   = j.contains("customer_phone") ? j["customer_phone"].as_string() : "";
            o.customer_address = j.contains("customer_address") ? j["customer_address"].as_string() : "";
            o.cargo_name       = j["cargo_name"].as_string();
            o.cargo_type       = static_cast<CargoType>(bounded_json_int(j["cargo_type"], 0, 3));
            o.cargo_weight     = j.contains("cargo_weight") ? j["cargo_weight"].as_double() : 0;
            o.cargo_quantity   = j.contains("cargo_quantity") ? bounded_json_int(j["cargo_quantity"], 1, 2147483647) : 1;
            o.cargo_volume     = j.contains("cargo_volume") ? j["cargo_volume"].as_double() : 0;
            o.sender_address   = j.contains("sender_address") ? j["sender_address"].as_string() : "";
            o.receiver_address = j.contains("receiver_address") ? j["receiver_address"].as_string() : "";
            o.creator          = actor.subject;

            auto result = order_svc.create_order(o);
            if (!result.ok) {
                res.status = 400;
                res.set_json(error_response(result.error).dump());
                return;
            }
            res.status = 201;
            res.set_json(Json::make_object({
                {"message",  "订单创建成功"},
                {"order_id", result.order.order_id},
                {"order",    json_order(result.order)}
            }).dump());
            LOG_INFO("创建订单: " + result.order.order_id);
        } catch (const RepositoryError&) {
            throw; // Storage failures are server errors, never malformed client input.
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_json(error_response(std::string("请求解析失败: ") + e.what()).dump());
        }
    });

    // ---- 订单列表（需认证） ----
    router.get("/api/orders", [&user_svc, &order_svc, jwt_secret, authorize](const http::Request& req, http::Response& res) {
        jwt::Claims actor;
        if (!authorize(req, res, {}, actor)) return;

        int page = bounded_query_int(req, "page", 1, 1000000);
        int page_size = bounded_query_int(req, "page_size", 20, 100);
        std::string keyword = req.query_params.count("keyword") ? req.query_params.at("keyword") : "";

        std::optional<OrderStatus> filter;
        if (req.query_params.count("status"))
            filter = status_filter(req.query_params.at("status"));

        auto result = order_svc.list_orders(page, page_size, keyword, filter);
        std::vector<Json> items;
        for (auto& o : result.items) items.push_back(json_order(o));
        res.set_json(paginated(items, result.total_count, result.current_page, result.total_pages).dump());
    });

    // ---- 订单详情（需认证） ----
    router.get("/api/orders/:id", [&user_svc, &order_svc, jwt_secret, authorize](const http::Request& req, http::Response& res) {
        jwt::Claims actor;
        if (!authorize(req, res, {}, actor)) return;

        std::string oid = req.path_params.at("id");
        auto opt = order_svc.find(oid);
        if (!opt) {
            res.status = 404;
            res.set_json(error_response("订单不存在", 404).dump());
            return;
        }
        res.set_json(Json::make_object({{"order", json_order(*opt)}}).dump());
    });

    // ---- 修改订单（需认证） ----
    router.put("/api/orders/:id", [&user_svc, &order_svc, jwt_secret, authorize](const http::Request& req, http::Response& res) {
        jwt::Claims actor;
        if (!authorize(req, res, {UserRole::CustomerSvc}, actor)) return;

        try {
            auto j = Json::parse(req.body);
            auto oid = req.path_params.at("id");
            auto opt = order_svc.find(oid);
            if (!opt) {
                res.status = 404;
                res.set_json(error_response("订单不存在", 404).dump());
                return;
            }
            if (!j.contains("edit_token")) {
                res.status=428;res.set_json(error_response("请先重新打开订单，获取最新内容后再修改",428).dump());return;
            }
            const auto revision=j["edit_token"].as_string();
            if(revision.size()!=64) throw std::invalid_argument("Invalid edit_token");
            Order o = *opt;
            if (j.contains("customer_name")) o.customer_name = j["customer_name"].as_string();
            if (j.contains("customer_phone")) o.customer_phone = j["customer_phone"].as_string();
            if (j.contains("cargo_name")) o.cargo_name = j["cargo_name"].as_string();
            if (j.contains("cargo_weight")) o.cargo_weight = j["cargo_weight"].as_double();
            if (j.contains("cargo_quantity")) o.cargo_quantity = bounded_json_int(j["cargo_quantity"],1,2147483647);
            if (j.contains("cargo_type")) o.cargo_type = static_cast<CargoType>(bounded_json_int(j["cargo_type"],0,3));
            if (j.contains("sender_address")) o.sender_address = j["sender_address"].as_string();
            if (j.contains("receiver_address")) o.receiver_address = j["receiver_address"].as_string();

            if (order_svc.modify(o,revision))
                res.set_json(ok_response("修改成功").dump());
            else {
                res.status = 409;
                res.set_json(error_response("订单已被其他人修改或处理，请保留输入并重新打开订单核对",409).dump());
            }
        } catch (const RepositoryError&) {
            throw; // Storage failures are server errors, never malformed client input.
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_json(error_response(e.what()).dump());
        }
    });

    // ---- 审核通过（需认证） ----
    router.post("/api/orders/:id/approve", [&user_svc, &order_svc, jwt_secret, authorize](const http::Request& req, http::Response& res) {
        jwt::Claims actor;
        if (!authorize(req, res, {UserRole::Admin}, actor)) return;

        auto oid = req.path_params.at("id");
        std::string auditor = actor.subject;
        try {
            auto j = Json::parse(req.body);
            // Identity always comes from the verified account.
        } catch (...) {}

        if (order_svc.approve(oid, auditor)) {
            res.set_json(ok_response("审核通过").dump());
            LOG_INFO("订单审核通过: " + oid);
        } else {
            res.status = 400;
            res.set_json(error_response("审核失败（订单不存在或状态已变更）").dump());
        }
    });

    // ---- 驳回（需认证） ----
    router.post("/api/orders/:id/reject", [&user_svc, &order_svc, jwt_secret, authorize](const http::Request& req, http::Response& res) {
        jwt::Claims actor;
        if (!authorize(req, res, {UserRole::Admin}, actor)) return;

        auto oid = req.path_params.at("id");
        try {
            auto j = Json::parse(req.body);
            std::string reason = j["reason"].as_string();
            std::string auditor = actor.subject;

            if (order_svc.reject(oid, reason, auditor)) {
                res.set_json(ok_response("已驳回").dump());
                LOG_INFO("订单驳回: " + oid);
            } else {
                res.status = 400;
                res.set_json(error_response("驳回失败").dump());
            }
        } catch (const RepositoryError&) {
            throw; // Storage failures are server errors, never malformed client input.
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_json(error_response(std::string("请提供驳回原因: ") + e.what()).dump());
        }
    });

    // ---- 出库（ToStock → ToTrans，需认证） ----
    router.post("/api/orders/:id/out-stock", [&user_svc, &order_svc, jwt_secret, authorize](const http::Request& req, http::Response& res) {
        jwt::Claims actor;
        if (!authorize(req, res, {UserRole::Storage}, actor)) return;

        auto oid = req.path_params.at("id");
        std::string op = actor.subject;
        try {
            auto j = Json::parse(req.body);
            // Ignore caller-supplied identity.
        } catch (...) {}

        if (order_svc.out_stock(oid, op)) {
            res.set_json(ok_response("出库成功，已转为待运输").dump());
            LOG_INFO("订单出库: " + oid);
        } else {
            res.status = 400;
            res.set_json(error_response("出库失败（订单不存在或状态不允许）").dump());
        }
    });

    // ---- 发车 / 开始运输（ToTrans → InTrans，需认证） ----
    router.post("/api/orders/:id/start-transport", [&user_svc, &order_svc, jwt_secret, authorize](const http::Request& req, http::Response& res) {
        jwt::Claims actor;
        if (!authorize(req, res, {UserRole::Dispatcher}, actor)) return;

        auto oid = req.path_params.at("id");
        std::string op = actor.subject;
        try {
            auto j = Json::parse(req.body);
            // Ignore caller-supplied identity.
        } catch (...) {}

        if (order_svc.start_transport(oid, op)) {
            res.set_json(ok_response("已发车，运输中").dump());
            LOG_INFO("订单发车: " + oid);
        } else {
            res.status = 400;
            res.set_json(error_response("发车失败（订单不存在或状态不允许）").dump());
        }
    });

    // ---- 送达确认（InTrans → Delivered，需认证） ----
    router.post("/api/orders/:id/deliver", [&user_svc, &order_svc, jwt_secret, authorize](const http::Request& req, http::Response& res) {
        jwt::Claims actor;
        if (!authorize(req, res, {UserRole::Dispatcher}, actor)) return;

        auto oid = req.path_params.at("id");
        std::string op = actor.subject;
        try {
            auto j = Json::parse(req.body);
            // Ignore caller-supplied identity.
        } catch (...) {}

        if (order_svc.deliver(oid, op)) {
            res.set_json(ok_response("已确认送达").dump());
            LOG_INFO("订单送达: " + oid);
        } else {
            res.status = 400;
            res.set_json(error_response("送达确认失败（订单不存在或状态不允许）").dump());
        }
    });

    // ---- 完成（Delivered → Completed，需认证） ----
    router.post("/api/orders/:id/complete", [&user_svc, &order_svc, jwt_secret, authorize](const http::Request& req, http::Response& res) {
        jwt::Claims actor;
        if (!authorize(req, res, {UserRole::CustomerSvc}, actor)) return;

        auto oid = req.path_params.at("id");
        std::string op = actor.subject;
        try {
            auto j = Json::parse(req.body);
            // Ignore caller-supplied identity.
        } catch (...) {}

        if (order_svc.complete(oid, op)) {
            res.set_json(ok_response("订单已完成").dump());
            LOG_INFO("订单完成: " + oid);
        } else {
            res.status = 400;
            res.set_json(error_response("完成失败（订单不存在或状态不允许）").dump());
        }
    });
}

} // namespace lms::api
