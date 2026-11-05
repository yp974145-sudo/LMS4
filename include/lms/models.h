#pragma once

#include "lms/common.h"
#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace lms {

// ========== 用户模型 ==========
struct User {
    int         id;
    std::string username;
    std::string password_hash;  // SHA-256 hex (64 chars)
    UserRole    role   = UserRole::CustomerSvc;
    UserStatus  status = UserStatus::Active;

    // 工厂方法：创建默认管理员
    static User make_admin(const std::string& name, const std::string& hash) {
        return {0, name, hash, UserRole::Admin, UserStatus::Active};
    }
};

// ========== 订单模型 ==========
struct Order {
    std::string  order_id;
    std::string  customer_name;
    std::string  customer_phone;
    std::string  customer_address;
    std::string  cargo_name;
    CargoType    cargo_type   = CargoType::Normal;
    double       cargo_weight  = 0.0;
    int          cargo_quantity = 0;
    double       cargo_volume  = 0.0;
    std::string  sender_address;
    std::string  receiver_address;
    OrderStatus  status       = OrderStatus::Pending;
    std::string  reject_reason;

    // 审计字段
    std::string  creator;
    std::string  create_time;
    std::string  auditor;
    std::string  audit_time;
};

// ========== 分页结果 ==========
template<typename T>
struct PageResult {
    std::vector<T> items;
    int total_count  = 0;
    int current_page = 1;
    int total_pages  = 1;
};

} // namespace lms
