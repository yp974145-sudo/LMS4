#include "lms/order_revision.h"
#include <gtest/gtest.h>
#include "lms/service.h"
#include "lms/crypto.h"
#include <map>
#include <vector>
#include <algorithm>

using namespace lms;

// ============================================
// Mock 仓库（内存实现）
// ============================================

class MockUserRepo : public IUserRepository {
    std::map<std::string, User> users_;
    int next_id_ = 1;

public:
    MockUserRepo() {
        // 预置一个管理员（密码 "admin123" 的 SHA-256）
        users_["admin"] = {1, "admin",
            crypto::sha256("admin123"),
            UserRole::Admin, UserStatus::Active};
        next_id_ = 2;
    }

    std::optional<User> verify_login(const std::string& username,
                                      const std::string& password_hash) override {
        auto it = users_.find(username);
        if (it != users_.end() && it->second.password_hash == password_hash
            && it->second.status == UserStatus::Active)
            return it->second;
        return std::nullopt;
    }

    std::optional<User> find_by_username(const std::string& username) override {
        auto it = users_.find(username);
        return it != users_.end() ? std::optional(it->second) : std::nullopt;
    }

    std::optional<User> create(const User& user) override {
        if (users_.count(user.username)) return std::nullopt;
        User nu = user;
        nu.id = next_id_++;
        users_[user.username] = nu;
        return nu;
    }

    PageResult<User> list_paged(int page, int page_size,
                                 const std::string& keyword) override {
        PageResult<User> result;
        std::vector<User> filtered;
        for (auto& [k, v] : users_) {
            if (keyword.empty() || v.username.find(keyword) != std::string::npos)
                filtered.push_back(v);
        }
        result.total_count = static_cast<int>(filtered.size());
        result.current_page = page;
        result.total_pages = std::max(1, (result.total_count + page_size - 1) / page_size);
        int start = (page - 1) * page_size;
        int end = std::min(start + page_size, result.total_count);
        for (int i = start; i < end; ++i) result.items.push_back(filtered[i]);
        return result;
    }

    bool reset_password(const std::string& username,
                         const std::string& new_hash) override {
        auto it = users_.find(username);
        if (it == users_.end()) return false;
        it->second.password_hash = new_hash;
        return true;
    }

    int total_count(const std::string& keyword) override {
        if (keyword.empty()) return static_cast<int>(users_.size());
        return static_cast<int>(std::count_if(users_.begin(), users_.end(),
            [&](auto& p) { return p.first.find(keyword) != std::string::npos; }));
    }
};

class MockOrderRepo : public IOrderRepository {
    std::map<std::string, Order> orders_;
public:
    std::optional<Order> find_by_id(const std::string& order_id) override {
        auto it = orders_.find(order_id);
        return it != orders_.end() ? std::optional(it->second) : std::nullopt;
    }

    bool insert(const Order& order) override {
        orders_[order.order_id] = order;
        return true;
    }

    bool update(const Order& order) override {
        if (!orders_.count(order.order_id)) return false;
        orders_[order.order_id] = order;
        return true;
    }

    bool update_if_status(const Order& order, OrderStatus expected) override {
        auto it=orders_.find(order.order_id);
        if(it==orders_.end() || it->second.status!=expected)return false;
        it->second=order;return true;
    }
    bool update_if_unchanged(const Order& order, const std::string& revision) override {
        auto it=orders_.find(order.order_id);
        if(it==orders_.end() || order_revision(it->second)!=revision)return false;
        it->second=order;return true;
    }

    PageResult<Order> list_paged(int page, int page_size,
                                  const std::string& keyword,
                                  std::optional<OrderStatus> filter) override {
        PageResult<Order> result;
        std::vector<Order> filtered;
        for (auto& [k, v] : orders_) {
            bool match = true;
            if (filter && v.status != *filter) match = false;
            if (!keyword.empty() &&
                v.order_id.find(keyword) == std::string::npos &&
                v.customer_name.find(keyword) == std::string::npos)
                match = false;
            if (match) filtered.push_back(v);
        }
        result.total_count = static_cast<int>(filtered.size());
        result.current_page = page;
        result.total_pages = std::max(1, (result.total_count + page_size - 1) / page_size);
        int start = (page - 1) * page_size;
        int end = std::min(start + page_size, result.total_count);
        for (int i = start; i < end; ++i) result.items.push_back(filtered[i]);
        return result;
    }

    int total_count(const std::string& keyword,
                    std::optional<OrderStatus> filter) override {
        return list_paged(1, 9999, keyword, filter).total_count;
    }
};

// ============================================
// UserService 测试
// ============================================

class UserServiceTest : public ::testing::Test {
protected:
    std::shared_ptr<MockUserRepo> repo = std::make_shared<MockUserRepo>();
    UserService svc{repo};
};

TEST_F(UserServiceTest, LoginSuccess) {
    auto user = svc.login("admin", "admin123");
    ASSERT_TRUE(user.has_value());
    EXPECT_EQ(user->username, "admin");
    EXPECT_EQ(user->role, UserRole::Admin);
}

TEST_F(UserServiceTest, LoginWrongPassword) {
    auto user = svc.login("admin", "wrongpassword");
    EXPECT_FALSE(user.has_value());
}

TEST_F(UserServiceTest, LoginEmptyUsername) {
    auto user = svc.login("", "admin123");
    EXPECT_FALSE(user.has_value());
}

TEST_F(UserServiceTest, CreateUserSuccess) {
    auto result = svc.create_user("newuser", "pass123", "pass123", UserRole::Storage);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.user.username, "newuser");
    EXPECT_GT(result.user.id, 0);
}

TEST_F(UserServiceTest, CreateUserDuplicate) {
    svc.create_user("dup", "pass123", "pass123", UserRole::Storage);
    auto result = svc.create_user("dup", "pass123", "pass123", UserRole::Storage);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, "用户名已存在");
}

TEST_F(UserServiceTest, CreateUserPasswordMismatch) {
    auto result = svc.create_user("user1", "pass1", "pass2", UserRole::Dispatcher);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, "两次密码不一致");
}

TEST_F(UserServiceTest, CreateUserShortPassword) {
    auto result = svc.create_user("user1", "12345", "12345", UserRole::Dispatcher);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, "密码长度至少 6 位");
}

TEST_F(UserServiceTest, ResetPassword) {
    EXPECT_TRUE(svc.reset_password("admin", "newpass123"));
    // 旧密码应失效
    EXPECT_FALSE(svc.login("admin", "admin123").has_value());
    // 新密码应生效
    EXPECT_TRUE(svc.login("admin", "newpass123").has_value());
}

TEST_F(UserServiceTest, ListUsers) {
    svc.create_user("u1", "password1", "password1", UserRole::Storage);
    svc.create_user("u2", "password2", "password2", UserRole::Dispatcher);
    auto result = svc.list_users(1, 10);
    EXPECT_GE(result.total_count, 3); // admin + u1 + u2
}

// ============================================
// OrderService 测试
// ============================================

class OrderServiceTest : public ::testing::Test {
protected:
    std::shared_ptr<MockOrderRepo> repo = std::make_shared<MockOrderRepo>();
    OrderService svc{repo};

    Order make_test_order() {
        Order o;
        o.customer_name = "测试客户";
        o.customer_phone = "13800138000";
        o.cargo_name = "测试货物";
        o.cargo_type = CargoType::Normal;
        o.cargo_weight = 10.0;
        o.cargo_quantity = 5;
        o.creator = "admin";
        return o;
    }
};

TEST_F(OrderServiceTest, CreateOrderSuccess) {
    auto result = svc.create_order(make_test_order());
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.order.status, OrderStatus::Pending);
    EXPECT_FALSE(result.order.order_id.empty());
}

TEST_F(OrderServiceTest, CreateOrderMissingName) {
    Order o = make_test_order();
    o.customer_name = "";
    auto result = svc.create_order(o);
    EXPECT_FALSE(result.ok);
}

TEST_F(OrderServiceTest, ApproveOrder) {
    auto cr = svc.create_order(make_test_order());
    ASSERT_TRUE(cr.ok);

    EXPECT_TRUE(svc.approve(cr.order.order_id, "admin"));
    auto found = svc.find(cr.order.order_id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->status, OrderStatus::ToStock);
}

TEST_F(OrderServiceTest, RejectOrder) {
    auto cr = svc.create_order(make_test_order());
    ASSERT_TRUE(cr.ok);

    EXPECT_TRUE(svc.reject(cr.order.order_id, "信息不全", "admin"));
    auto found = svc.find(cr.order.order_id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->status, OrderStatus::Rejected);
    EXPECT_EQ(found->reject_reason, "信息不全");
}

TEST_F(OrderServiceTest, CannotApproveNonPending) {
    auto cr = svc.create_order(make_test_order());
    svc.approve(cr.order.order_id, "admin"); // 第一次审核

    // 第二次审核应失败（已不是 Pending 状态）
    EXPECT_FALSE(svc.approve(cr.order.order_id, "admin"));
}

TEST_F(OrderServiceTest, ModifyOrder) {
    auto cr = svc.create_order(make_test_order());
    ASSERT_TRUE(cr.ok);

    Order modified = *svc.find(cr.order.order_id);
    modified.customer_name = "修改后的客户";
    EXPECT_TRUE(svc.modify(modified,order_revision(*repo->find_by_id(modified.order_id))));

    auto found = svc.find(cr.order.order_id);
    EXPECT_EQ(found->customer_name, "修改后的客户");
}

TEST_F(OrderServiceTest, CannotModifyApprovedOrder) {
    auto cr = svc.create_order(make_test_order());
    svc.approve(cr.order.order_id, "admin");

    Order modified = *svc.find(cr.order.order_id);
    modified.customer_name = "修改已审核订单";
    EXPECT_FALSE(svc.modify(modified,order_revision(*repo->find_by_id(modified.order_id))));
}

TEST_F(OrderServiceTest, FindNonExistent) {
    auto found = svc.find("NONEXISTENT");
    EXPECT_FALSE(found.has_value());
}
