#pragma once
#include <optional>
#include <string>

namespace lesson {
enum class Role { Admin, Customer, Storage, Dispatch };
enum class Status { Pending, Approved, Rejected };
struct User { std::string username; Role role; };
struct Order {
    std::string id, customer, cargo, creator;
    int quantity = 0;
    Status status = Status::Pending;
};
inline std::optional<std::string> validate(const Order& order) {
    if(order.customer.empty()) return "customer required";
    if(order.cargo.empty()) return "cargo required";
    if(order.quantity<=0) return "quantity must be positive";
    return std::nullopt;
}
}
