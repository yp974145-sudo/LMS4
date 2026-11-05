#include "lms/order_revision.h"
#include "lms/service.h"
#include "lms/crypto.h"
#include <algorithm>
#include <cmath>

namespace lms {

// ========== UserService ==========
std::optional<User> UserService::current_user(const std::string& name) {
    auto user = repo_->find_by_username(name);
    if (!user || user->status != UserStatus::Active) return std::nullopt;
    return user;
}


std::optional<User> UserService::login(const std::string& username,
                                         const std::string& password) {
    if (username.empty() || password.empty()) return std::nullopt;
    // 对输入密码做 SHA-256，与存储的哈希比对
    return repo_->verify_login(username, crypto::sha256(password));
}

UserService::CreateResult UserService::create_user(
        const std::string& username,
        const std::string& password,
        const std::string& confirm_pwd,
        UserRole role) {

    if (username.empty() || password.empty())
        return {false, "用户名和密码不能为空", {}};

    if (password != confirm_pwd)
        return {false, "两次密码不一致", {}};

    if (username.size() > 49 || password.size() > 49)
        return {false, "用户名或密码过长", {}};

    // 密码长度校验：至少 6 位
    if (password.size() < 6)
        return {false, "密码长度至少 6 位", {}};

    if (static_cast<int>(role) < 0 || static_cast<int>(role) > 3)
        return {false, "Invalid role", {}};
    User nu;
    nu.username      = username;
    nu.password_hash = crypto::sha256(password);  // 存储 SHA-256 哈希
    nu.role          = role;
    nu.status        = UserStatus::Active;

    auto result = repo_->create(nu);
    if (!result)
        return {false, "用户名已存在", {}};

    return {true, "", *result};
}

PageResult<User> UserService::list_users(int page, int page_size,
                                           const std::string& keyword) {
    return repo_->list_paged(page, page_size, keyword);
}

bool UserService::reset_password(const std::string& username,
                                   const std::string& new_pwd) {
    if (username.empty()) return false;
    // 重置的密码也要哈希存储
    return repo_->reset_password(username, crypto::sha256(new_pwd));
}

// ========== OrderService ==========

std::optional<std::string> OrderService::validate(const Order& o) {
    if (!std::isfinite(o.cargo_weight) || !std::isfinite(o.cargo_volume)) return "Invalid number";
    if (static_cast<int>(o.cargo_type) > 3) return "Invalid cargo type";
    for (const auto* value : {&o.customer_name, &o.customer_phone, &o.customer_address,
            &o.cargo_name, &o.sender_address, &o.receiver_address, &o.creator})
        if (value->size() > 255) return "Field too long";
    if (o.customer_name.empty())  return "客户姓名不能为空";
    if (o.cargo_name.empty())     return "货物名称不能为空";
    if (o.cargo_weight < 0 || o.cargo_volume < 0)       return "货物重量不能为负";
    if (o.cargo_quantity <= 0)    return "货物数量必须大于0";
    return std::nullopt;
}

OrderService::CreateOrderResult OrderService::create_order(const Order& order) {
    std::lock_guard<std::mutex> guard(order_mutex_);
    auto err = validate(order);
    if (err) return {false, *err, {}};

    Order no = order;
    no.order_id   = generate_order_id();
    no.status     = OrderStatus::Pending;
    no.creator    = order.creator;
    no.create_time = now_str();

    if (!repo_->insert(no))
        return {false, "数据库写入失败", {}};

    return {true, "", no};
}

bool OrderService::approve(const std::string& order_id,
                             const std::string& auditor) {
    std::lock_guard<std::mutex> guard(order_mutex_);
    auto opt = repo_->find_by_id(order_id);
    if (!opt || opt->status != OrderStatus::Pending) return false;

    Order o = *opt;
    o.status      = OrderStatus::ToStock;
    o.reject_reason.clear();
    o.auditor     = auditor;
    o.audit_time  = now_str();
    return repo_->update_if_unchanged(o,order_revision(*opt));
}

bool OrderService::reject(const std::string& order_id,
                            const std::string& reason,
                            const std::string& auditor) {
    std::lock_guard<std::mutex> guard(order_mutex_);
    if (reason.empty() || reason.size() > 255) return false;

    auto opt = repo_->find_by_id(order_id);
    if (!opt || opt->status != OrderStatus::Pending) return false;

    Order o = *opt;
    o.status        = OrderStatus::Rejected;
    o.reject_reason = reason;
    o.auditor       = auditor;
    o.audit_time    = now_str();
    return repo_->update_if_unchanged(o,order_revision(*opt));
}

bool OrderService::out_stock(const std::string& order_id,
                            const std::string& operator_name) {
    std::lock_guard<std::mutex> guard(order_mutex_);
    auto opt = repo_->find_by_id(order_id);
    if (!opt || opt->status != OrderStatus::ToStock) return false;

    Order o = *opt;
    o.status     = OrderStatus::ToTrans;
    o.auditor    = operator_name;
    o.audit_time = now_str();
    return repo_->update_if_unchanged(o,order_revision(*opt));
}

bool OrderService::start_transport(const std::string& order_id,
                                    const std::string& operator_name) {
    std::lock_guard<std::mutex> guard(order_mutex_);
    auto opt = repo_->find_by_id(order_id);
    if (!opt || opt->status != OrderStatus::ToTrans) return false;

    Order o = *opt;
    o.status     = OrderStatus::InTrans;
    o.auditor    = operator_name;
    o.audit_time = now_str();
    return repo_->update_if_unchanged(o,order_revision(*opt));
}

bool OrderService::deliver(const std::string& order_id,
                            const std::string& operator_name) {
    std::lock_guard<std::mutex> guard(order_mutex_);
    auto opt = repo_->find_by_id(order_id);
    if (!opt || opt->status != OrderStatus::InTrans) return false;

    Order o = *opt;
    o.status     = OrderStatus::Delivered;
    o.auditor    = operator_name;
    o.audit_time = now_str();
    return repo_->update_if_unchanged(o,order_revision(*opt));
}

bool OrderService::complete(const std::string& order_id,
                             const std::string& operator_name) {
    std::lock_guard<std::mutex> guard(order_mutex_);
    auto opt = repo_->find_by_id(order_id);
    if (!opt || opt->status != OrderStatus::Delivered) return false;

    Order o = *opt;
    o.status     = OrderStatus::Completed;
    o.auditor    = operator_name;
    o.audit_time = now_str();
    return repo_->update_if_unchanged(o,order_revision(*opt));
}

bool OrderService::modify(const Order& updated, const std::string& revision) {
    std::lock_guard<std::mutex> guard(order_mutex_);
    auto opt = repo_->find_by_id(updated.order_id);
    if (!opt || opt->status != OrderStatus::Pending) return false;

    auto err = validate(updated);
    if (err) throw std::invalid_argument(*err);
    if (order_revision(*opt)!=revision) return false;
    return repo_->update_if_unchanged(updated,revision);
}

PageResult<Order> OrderService::list_orders(int page, int page_size,
                                              const std::string& keyword,
                                              std::optional<OrderStatus> filter) {
    std::lock_guard<std::mutex> guard(order_mutex_);
    return repo_->list_paged(page, page_size, keyword, filter);
}

std::optional<Order> OrderService::find(const std::string& order_id) {
    std::lock_guard<std::mutex> guard(order_mutex_);
    return repo_->find_by_id(order_id);
}

} // namespace lms
