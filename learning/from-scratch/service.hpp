#pragma once
#include "model.hpp"
#include <map>
#include <memory>
#include <mutex>
#include <utility>

namespace lesson {
class IOrderRepository {
public:
    virtual ~IOrderRepository()=default;
    virtual bool insert(const Order& order)=0;
    virtual std::optional<Order> find(const std::string& id)=0;
    virtual bool approve_if_pending(const std::string& id)=0;
};
class MemoryOrderRepository final : public IOrderRepository {
    std::map<std::string,Order> orders_;
public:
    bool insert(const Order& order) override {
        return orders_.emplace(order.id,order).second;
    }
    std::optional<Order> find(const std::string& id) override {
        auto it=orders_.find(id);
        if(it==orders_.end()) return std::nullopt;
        return it->second;
    }
    bool approve_if_pending(const std::string& id) override {
        auto it=orders_.find(id);
        if(it==orders_.end() || it->second.status!=Status::Pending) return false;
        it->second.status=Status::Approved;return true;
    }
};
// This small prototype owns all access through a single service mutex.
// Production repositories must provide their own atomicity across callers/processes.
class OrderService {
    std::shared_ptr<IOrderRepository> repo_;
    std::mutex mutex_;
    int next_id_=0;
public:
    struct Result { bool ok;std::string error;Order order; };
    explicit OrderService(std::shared_ptr<IOrderRepository> repo):repo_(std::move(repo)){}
    Result create(Order input,const User& actor) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(actor.role!=Role::Customer && actor.role!=Role::Admin)
            return {false,"forbidden",{}};
        if(auto error=validate(input)) return {false,*error,{}};
        input.id="DEMO-"+std::to_string(++next_id_);
        input.creator=actor.username;input.status=Status::Pending;
        if(!repo_->insert(input)) return {false,"insert failed",{}};
        return {true,"",input};
    }
    bool approve(const std::string& id,const User& actor) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(actor.role!=Role::Admin) return false;
        return repo_->approve_if_pending(id);
    }
    std::optional<Order> find(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return repo_->find(id);
    }
};
}
