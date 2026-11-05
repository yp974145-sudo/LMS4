#include "lms/repository.h"
#include "lms/order_revision.h"
#include "lms/file_records.h"
#include <fstream>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <algorithm>

namespace lms {

// ========== 文件版订单仓库 ==========
class OrderRepoFile : public IOrderRepository {
    inline static std::mutex file_mutex_;
    std::filesystem::path filename_;

    std::vector<Order> read_all() const { return file_records::read_orders(filename_); }

    void write_all(const std::vector<Order>& orders) const {
        file_storage::save(filename_, 'O', file_records::encode_orders(orders));
    }

public:
    explicit OrderRepoFile(const std::string& directory) : filename_(std::filesystem::path(directory)/"order.dat") {
        std::lock_guard<std::mutex> guard(file_mutex_);
        if (file_storage::exists(filename_)) { read_all(); return; }
        if (file_storage::exists(file_storage::backup_path(filename_)))
            throw file_storage::StorageError("Order snapshot missing; explicit recovery from .bak required");
        write_all({});
    }

    std::optional<Order> find_by_id(const std::string& order_id) override {
        std::lock_guard<std::mutex> guard(file_mutex_);
        auto orders = read_all();
        for (const auto& o : orders)
            if (o.order_id == order_id) return o;
        return std::nullopt;
    }

    bool insert(const Order& order) override {
        std::lock_guard<std::mutex> guard(file_mutex_);
        auto orders = read_all();
        if (std::any_of(orders.begin(), orders.end(), [&](const Order& existing) { return existing.order_id == order.order_id; })) return false;
        orders.push_back(order);
        write_all(orders);
        return true;
    }

    bool update(const Order& order) override {
        std::lock_guard<std::mutex> guard(file_mutex_);
        auto orders = read_all();
        for (auto& o : orders) {
            if (o.order_id == order.order_id) {
                o = order;
                write_all(orders);
                return true;
            }
        }
        return false;
    }

    bool update_if_status(const Order& order, OrderStatus expected) override {
        std::lock_guard<std::mutex> guard(file_mutex_);
        auto orders=read_all();
        for(auto& current:orders) {
            if(current.order_id==order.order_id) {
                if(current.status!=expected)return false;
                current=order;write_all(orders);return true;
            }
        }
        return false;
    }

    bool update_if_unchanged(const Order& order, const std::string& revision) override {
        std::lock_guard<std::mutex> guard(file_mutex_);
        auto orders=read_all();
        for(auto& current:orders) {
            if(current.order_id!=order.order_id) continue;
            if(order_revision(current)!=revision) return false;
            if(order_revision(order)==revision) return true;
            current=order;write_all(orders);return true;
        }
        return false;
    }

    PageResult<Order> list_paged(int page, int page_size,
                                  const std::string& keyword,
                                  std::optional<OrderStatus> filter) override {
        std::lock_guard<std::mutex> guard(file_mutex_);
        if (page < 1 || page > 1000000 || page_size < 1 || page_size > 100)
            throw std::invalid_argument("Invalid pagination");
        auto orders = read_all();
        PageResult<Order> result;

        std::vector<Order> filtered;
        for (const auto& o : orders) {
            if (filter && o.status != *filter) continue;
            if (!keyword.empty()) {
                if (o.order_id.find(keyword) == std::string::npos &&
                    o.customer_name.find(keyword) == std::string::npos)
                    continue;
            }
            filtered.push_back(o);
        }

        result.total_count  = static_cast<int>(filtered.size());
        result.current_page = page;
        result.total_pages  = std::max(1, (result.total_count + page_size - 1) / page_size);

        int start = (page - 1) * page_size;
        int end   = std::min(start + page_size, result.total_count);
        for (int i = start; i < end; ++i)
            result.items.push_back(filtered[i]);

        return result;
    }

    int total_count(const std::string& keyword,
                    std::optional<OrderStatus> filter) override {
        std::lock_guard<std::mutex> guard(file_mutex_);
        auto orders = read_all();
        int count = 0;
        for (const auto& o : orders) {
            if (filter && o.status != *filter) continue;
            if (!keyword.empty()) {
                if (o.order_id.find(keyword) == std::string::npos &&
                    o.customer_name.find(keyword) == std::string::npos)
                    continue;
            }
            ++count;
        }
        return count;
    }
};

} // namespace lms

// ========== 工厂函数 ==========
std::shared_ptr<lms::IOrderRepository> make_order_repo_file(const std::string& directory) {
    return std::make_shared<lms::OrderRepoFile>(directory);
}
