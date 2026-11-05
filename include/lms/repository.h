#pragma once

#include "lms/models.h"
#include <memory>
#include <vector>
#include <optional>

namespace lms {

// ========== 用户数据访问接口 ==========
class IUserRepository {
public:
    virtual ~IUserRepository() = default;

    // 验证登录，成功返回完整 User
    virtual std::optional<User> verify_login(const std::string& username,
                                              const std::string& password) = 0;

    // 按用户名查找
    virtual std::optional<User> find_by_username(const std::string& username) = 0;

    // 创建用户（带查重），成功返回带 ID 的 User
    virtual std::optional<User> create(const User& user) = 0;

    // 分页模糊查询
    virtual PageResult<User> list_paged(int page, int page_size,
                                         const std::string& keyword = "") = 0;

    // 重置密码
    virtual bool reset_password(const std::string& username,
                                const std::string& new_pwd = "888888") = 0;

    // 获取总数（用于分页）
    virtual int total_count(const std::string& keyword = "") = 0;
};

// ========== 订单数据访问接口 ==========
class IOrderRepository {
public:
    virtual ~IOrderRepository() = default;

    virtual std::optional<Order> find_by_id(const std::string& order_id) = 0;

    virtual bool insert(const Order& order) = 0;

    virtual bool update(const Order& order) = 0;

    // Check and write atomically inside the repository, not only in one service process.
    virtual bool update_if_status(const Order& order, OrderStatus expected) = 0;
    virtual bool update_if_unchanged(const Order& order, const std::string& revision) = 0;

    virtual PageResult<Order> list_paged(int page, int page_size,
                                          const std::string& keyword = "",
                                          std::optional<OrderStatus> status_filter = {}) = 0;

    virtual int total_count(const std::string& keyword = "",
                            std::optional<OrderStatus> status_filter = {}) = 0;
};

} // namespace lms
