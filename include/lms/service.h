#pragma once

#include "lms/models.h"
#include "lms/repository.h"
#include <memory>
#include <string>
#include <optional>
#include <mutex>

namespace lms {

// ========== 用户业务逻辑 ==========
class UserService {
public:
    explicit UserService(std::shared_ptr<IUserRepository> repo)
        : repo_(std::move(repo)) {}

    std::optional<User> current_user(const std::string& username);

    // 登录：返回登录成功的用户，失败返回 nullopt
    std::optional<User> login(const std::string& username,
                               const std::string& password);

    // 注册：校验 + 创建，失败返回错误信息
    struct CreateResult {
        bool ok;
        std::string error;
        User user;
    };
    CreateResult create_user(const std::string& username,
                              const std::string& password,
                              const std::string& confirm_pwd,
                              UserRole role);

    // 分页查询
    PageResult<User> list_users(int page, int page_size,
                                 const std::string& keyword = "");

    // 重置密码
    bool reset_password(const std::string& target_username,
                         const std::string& new_pwd = "888888");

private:
    std::shared_ptr<IUserRepository> repo_;
};

// ========== 订单业务逻辑 ==========
class OrderService {
public:
    explicit OrderService(std::shared_ptr<IOrderRepository> repo)
        : repo_(std::move(repo)) {}

    // 创建订单
    struct CreateOrderResult {
        bool ok;
        std::string error;
        Order order;
    };
    CreateOrderResult create_order(const Order& order);

    // 审核通过
    bool approve(const std::string& order_id, const std::string& auditor);

    // 驳回
    bool reject(const std::string& order_id,
                const std::string& reason,
                const std::string& auditor);

    // 出库（ToStock → ToTrans）
    bool out_stock(const std::string& order_id, const std::string& operator_name);

    // 发车 / 开始运输（ToTrans → InTrans）
    bool start_transport(const std::string& order_id, const std::string& operator_name);

    // 送达确认（InTrans → Delivered）
    bool deliver(const std::string& order_id, const std::string& operator_name);

    // 完成（Delivered → Completed）
    bool complete(const std::string& order_id, const std::string& operator_name);

    // 修改（仅待审核状态允许）
    bool modify(const Order& updated, const std::string& revision);

    // 分页查询
    PageResult<Order> list_orders(int page, int page_size,
                                   const std::string& keyword = "",
                                   std::optional<OrderStatus> filter = {});

    // 按 ID 查找
    std::optional<Order> find(const std::string& order_id);

private:
    std::shared_ptr<IOrderRepository> repo_;
    std::mutex order_mutex_;

    // 校验订单字段
    std::optional<std::string> validate(const Order& order);
};

} // namespace lms
