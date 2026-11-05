#pragma once

#include <string>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <cstdio>
#include <chrono>
#include <algorithm>

namespace lms {

// ========== 枚举：用 enum class 替代 C 风格 enum ==========

enum class UserRole : uint8_t {
    Admin        = 0,
    Storage      = 1,
    Dispatcher   = 2,
    CustomerSvc  = 3
};

enum class UserStatus : uint8_t {
    Active = 0,
    Locked = 1
};

enum class CargoType : uint8_t {
    Normal    = 0,
    Fragile   = 1,
    ColdChain = 2,
    Dangerous = 3
};

enum class OrderStatus : uint8_t {
    Pending    = 0,   // 待审核
    Rejected   = 1,   // 已驳回
    ToStock    = 2,   // 待出库
    ToTrans    = 3,   // 待运输
    InTrans    = 4,   // 运输中
    Delivered  = 5,   // 已送达
    Completed  = 6    // 已完成
};

// ========== 工具函数 ==========

inline const char* role_name(UserRole r) {
    switch (r) {
        case UserRole::Admin:       return "管理员";
        case UserRole::Storage:     return "仓储员";
        case UserRole::Dispatcher:  return "调度员";
        case UserRole::CustomerSvc: return "客服专员";
    }
    return "未知";
}

inline const char* status_name(OrderStatus s) {
    switch (s) {
        case OrderStatus::Pending:   return "待审核";
        case OrderStatus::Rejected:  return "已驳回";
        case OrderStatus::ToStock:   return "待出库";
        case OrderStatus::ToTrans:   return "待运输";
        case OrderStatus::InTrans:   return "运输中";
        case OrderStatus::Delivered: return "已送达";
        case OrderStatus::Completed: return "已完成";
    }
    return "未知";
}

inline const char* cargo_name(CargoType c) {
    switch (c) {
        case CargoType::Normal:    return "普通";
        case CargoType::Fragile:   return "易碎";
        case CargoType::ColdChain: return "冷链";
        case CargoType::Dangerous: return "危险品";
    }
    return "未知";
}

// ========== 时间戳工具 ==========

inline std::tm local_time(time_t t) {
    std::tm out{};
#ifdef _WIN32
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
    return out;
}
inline std::string now_str() {
    auto t = local_time(time(nullptr));
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    return buf;
}
inline std::string generate_order_id() {
    static std::mutex mutex;
    static unsigned long long last = 0;
    std::lock_guard<std::mutex> guard(mutex);
    auto current = static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    last = std::max(current, last + 1);
    return "WL" + std::to_string(last);
}
} // namespace lms
