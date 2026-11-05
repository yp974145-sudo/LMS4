#include "lms/order_revision.h"
#ifdef LMS_HAS_MYSQL

#include "lms/repository.h"
#include "lms/crypto.h"
#include "lms/connection_pool.h"
#include <mysql.h>
#include <memory>
#include <stdexcept>
#include <cstdio>

namespace lms {

// ============================================
// MySQL 版用户仓库（参数化查询）
// ============================================
class UserRepoMysql : public IUserRepository {
public:
    UserRepoMysql() {
        {
        auto lease = ConnectionPool::instance().acquire();
        lease->exec(
            "CREATE TABLE IF NOT EXISTS users ("
            "id INT AUTO_INCREMENT PRIMARY KEY,"
            "username VARCHAR(255) NOT NULL UNIQUE,"
            "password_hash VARCHAR(255) NOT NULL,"
            "role TINYINT NOT NULL DEFAULT 3,"
            "status TINYINT NOT NULL DEFAULT 0"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
        );
        }
        ensure_defaults();
    }

    std::optional<User> verify_login(const std::string& username,
                                      const std::string& password_hash) override {
        auto lease = ConnectionPool::instance().acquire();
        auto stmt = lease->prepare(
            "SELECT id, username, password_hash, role, status "
            "FROM users WHERE username=? AND password_hash=? AND status=0"
        );
        stmt.bind_string(0, username);
        stmt.bind_string(1, password_hash);
        if (!stmt.execute()) return std::nullopt;
        if (!stmt.store_result()) return std::nullopt;

        std::vector<std::string> row;
        if (!stmt.fetch_row(row) || row.size() < 5) return std::nullopt;

        return User{
            std::stoi(row[0]), row[1], row[2],
            static_cast<UserRole>(std::stoi(row[3])),
            UserStatus::Active
        };
    }

    std::optional<User> find_by_username(const std::string& username) override {
        auto lease = ConnectionPool::instance().acquire();
        auto stmt = lease->prepare(
            "SELECT id, username, password_hash, role, status "
            "FROM users WHERE username=?"
        );
        stmt.bind_string(0, username);
        if (!stmt.execute()) return std::nullopt;
        if (!stmt.store_result()) return std::nullopt;

        std::vector<std::string> row;
        if (!stmt.fetch_row(row) || row.size() < 5) return std::nullopt;

        return User{
            std::stoi(row[0]), row[1], row[2],
            static_cast<UserRole>(std::stoi(row[3])),
            static_cast<UserStatus>(std::stoi(row[4]))
        };
    }

    std::optional<User> create(const User& user) override {
        auto lease = ConnectionPool::instance().acquire();

        // 查重
        {
            auto stmt = lease->prepare("SELECT id FROM users WHERE username=?");
            stmt.bind_string(0, user.username);
            stmt.execute();
            stmt.store_result();
            std::vector<std::string> row;
            if (stmt.fetch_row(row)) return std::nullopt; // 已存在
        }

        // 插入
        auto stmt = lease->prepare(
            "INSERT INTO users (username, password_hash, role, status) VALUES (?,?,?,?)"
        );
        stmt.bind_string(0, user.username);
        stmt.bind_string(1, user.password_hash);
        stmt.bind_int(2, static_cast<int>(user.role));
        stmt.bind_int(3, static_cast<int>(user.status));

        if (!stmt.execute()) return std::nullopt;

        User nu = user;
        nu.id = static_cast<int>(stmt.last_insert_id());
        return nu;
    }

    PageResult<User> list_paged(int page, int page_size,
                                 const std::string& keyword) override {
        if(page<1 || page>1000000 || page_size<1 || page_size>100)throw std::invalid_argument("Invalid pagination");
        PageResult<User> result;
        auto lease = ConnectionPool::instance().acquire();

        // 总数
        if (keyword.empty()) {
            auto stmt = lease->prepare("SELECT COUNT(*) FROM users");
            stmt.execute();
            stmt.store_result();
            std::vector<std::string> row;
            if (stmt.fetch_row(row)) result.total_count = std::stoi(row[0]);
        } else {
            auto stmt = lease->prepare(
                "SELECT COUNT(*) FROM users WHERE username LIKE CONCAT('%',?,'%')"
            );
            stmt.bind_string(0, keyword);
            stmt.execute();
            stmt.store_result();
            std::vector<std::string> row;
            if (stmt.fetch_row(row)) result.total_count = std::stoi(row[0]);
        }

        result.current_page = page;
        result.total_pages  = std::max(1, (result.total_count + page_size - 1) / page_size);
        int offset = (page - 1) * page_size;

        // 分页数据
        auto stmt = keyword.empty()
            ? lease->prepare(
                "SELECT id, username, password_hash, role, status "
                "FROM users ORDER BY id LIMIT ? OFFSET ?"
              )
            : lease->prepare(
                "SELECT id, username, password_hash, role, status "
                "FROM users WHERE username LIKE CONCAT('%',?,'%') "
                "ORDER BY id LIMIT ? OFFSET ?"
              );

        int param_idx = 0;
        if (!keyword.empty()) stmt.bind_string(param_idx++, keyword);
        stmt.bind_int(param_idx++, page_size);
        stmt.bind_int(param_idx, offset);

        if (!stmt.execute()) return result;
        if (!stmt.store_result()) return result;

        std::vector<std::string> row;
        while (stmt.fetch_row(row)) {
            if (row.size() < 5) continue;
            result.items.push_back(User{
                std::stoi(row[0]), row[1], row[2],
                static_cast<UserRole>(std::stoi(row[3])),
                static_cast<UserStatus>(std::stoi(row[4]))
            });
        }
        return result;
    }

    bool reset_password(const std::string& username,
                         const std::string& new_hash) override {
        auto lease = ConnectionPool::instance().acquire();
        auto stmt = lease->prepare("UPDATE users SET password_hash=? WHERE username=?");
        stmt.bind_string(0, new_hash);
        stmt.bind_string(1, username);
        return stmt.execute() && stmt.affected_rows() > 0;
    }

    int total_count(const std::string& keyword) override {
        auto lease = ConnectionPool::instance().acquire();
        if (keyword.empty()) {
            auto stmt = lease->prepare("SELECT COUNT(*) FROM users");
            stmt.execute(); stmt.store_result();
            std::vector<std::string> row;
            return stmt.fetch_row(row) ? std::stoi(row[0]) : 0;
        } else {
            auto stmt = lease->prepare(
                "SELECT COUNT(*) FROM users WHERE username LIKE CONCAT('%',?,'%')"
            );
            stmt.bind_string(0, keyword);
            stmt.execute(); stmt.store_result();
            std::vector<std::string> row;
            return stmt.fetch_row(row) ? std::stoi(row[0]) : 0;
        }
    }

private:
    void ensure_defaults() {
        auto lease = ConnectionPool::instance().acquire();
        const std::pair<const char*,int> defaults[]={{"admin",0},{"storage",1},{"dispatch",2},{"customer",3}};
        for(const auto& entry:defaults) {
            auto statement=lease->prepare("INSERT IGNORE INTO users (username,password_hash,role,status) VALUES (?,?,?,0)");
            statement.bind_string(0,entry.first);
            statement.bind_string(1,crypto::sha256(std::string(entry.first)+"123"));
            statement.bind_int(2,entry.second);
            if(!statement.execute())throw DatabaseError("Cannot seed demo account");
        }
    }
};

// ============================================
// MySQL 版订单仓库（参数化查询）
// ============================================
class OrderRepoMysql : public IOrderRepository {
public:
    OrderRepoMysql() {
        auto lease = ConnectionPool::instance().acquire();
        lease->exec(
            "CREATE TABLE IF NOT EXISTS orders ("
            "order_id VARCHAR(25) PRIMARY KEY,"
            "customer_name VARCHAR(255) NOT NULL,"
            "customer_phone VARCHAR(255) DEFAULT '',"
            "customer_address VARCHAR(255) DEFAULT '',"
            "cargo_name VARCHAR(255) NOT NULL,"
            "cargo_type TINYINT DEFAULT 0,"
            "cargo_weight DOUBLE DEFAULT 0,"
            "cargo_quantity INT DEFAULT 1,"
            "cargo_volume DOUBLE DEFAULT 0,"
            "sender_address VARCHAR(255) DEFAULT '',"
            "receiver_address VARCHAR(255) DEFAULT '',"
            "status TINYINT DEFAULT 0,"
            "reject_reason VARCHAR(255) DEFAULT '',"
            "creator VARCHAR(255) DEFAULT '',"
            "create_time VARCHAR(30) DEFAULT '',"
            "auditor VARCHAR(255) DEFAULT '',"
            "audit_time VARCHAR(30) DEFAULT '',"
            "INDEX idx_status (status),"
            "INDEX idx_status_order (status, order_id),"
            "INDEX idx_customer (customer_name)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
        );
    }

    std::optional<Order> find_by_id(const std::string& order_id) override {
        auto lease = ConnectionPool::instance().acquire();
        auto stmt = lease->prepare("SELECT order_id,customer_name,customer_phone,customer_address,cargo_name,cargo_type,cargo_weight,cargo_quantity,cargo_volume,sender_address,receiver_address,status,reject_reason,creator,create_time,auditor,audit_time FROM orders WHERE order_id=?");
        stmt.bind_string(0, order_id);
        if (!stmt.execute()) return std::nullopt;
        if (!stmt.store_result()) return std::nullopt;

        std::vector<std::string> row;
        if (!stmt.fetch_row(row) || row.size() < 17) return std::nullopt;
        return row_to_order(row);
    }

    bool insert(const Order& order) override {
        auto lease = ConnectionPool::instance().acquire();
        auto stmt = lease->prepare(
            "INSERT INTO orders (order_id, customer_name, customer_phone, customer_address,"
            "cargo_name, cargo_type, cargo_weight, cargo_quantity, cargo_volume,"
            "sender_address, receiver_address, status, reject_reason,"
            "creator, create_time, auditor, audit_time) VALUES "
            "(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"
        );
        int i = 0;
        stmt.bind_string(i++, order.order_id);
        stmt.bind_string(i++, order.customer_name);
        stmt.bind_string(i++, order.customer_phone);
        stmt.bind_string(i++, order.customer_address);
        stmt.bind_string(i++, order.cargo_name);
        stmt.bind_int(i++, static_cast<int>(order.cargo_type));
        stmt.bind_double(i++, order.cargo_weight);
        stmt.bind_int(i++, order.cargo_quantity);
        stmt.bind_double(i++, order.cargo_volume);
        stmt.bind_string(i++, order.sender_address);
        stmt.bind_string(i++, order.receiver_address);
        stmt.bind_int(i++, static_cast<int>(order.status));
        stmt.bind_string(i++, order.reject_reason);
        stmt.bind_string(i++, order.creator);
        stmt.bind_string(i++, order.create_time);
        stmt.bind_string(i++, order.auditor);
        stmt.bind_string(i++, order.audit_time);
        return stmt.execute();
    }

    bool update(const Order& order) override { return perform_update(order,std::nullopt); }
    bool update_if_status(const Order& order, OrderStatus expected) override { return perform_update(order,expected); }
    bool update_if_unchanged(const Order& order, const std::string& revision) override {
        auto lease=ConnectionPool::instance().acquire();
        Transaction transaction(*lease);
        auto stmt=lease->prepare("SELECT order_id,customer_name,customer_phone,customer_address,cargo_name,cargo_type,cargo_weight,cargo_quantity,cargo_volume,sender_address,receiver_address,status,reject_reason,creator,create_time,auditor,audit_time FROM orders WHERE order_id=? FOR UPDATE");
        stmt.bind_string(0,order.order_id);stmt.execute();stmt.store_result();
        std::vector<std::string> row;
        if(!stmt.fetch_row(row) || row.size()<17 || order_revision(row_to_order(row))!=revision) return false;
        if(order_revision(order)==revision) {transaction.commit();return true;}
        if(!perform_update_on(*lease,order,std::nullopt)) return false;
        transaction.commit();return true;
    }
private:
    bool perform_update(const Order& order, std::optional<OrderStatus> expected) {
        auto lease=ConnectionPool::instance().acquire();
        return perform_update_on(*lease,order,expected);
    }
    bool perform_update_on(MysqlConn& connection, const Order& order, std::optional<OrderStatus> expected) {
        const std::string sql =
            "UPDATE orders SET customer_name=?, customer_phone=?, customer_address=?,"
            "cargo_name=?, cargo_type=?, cargo_weight=?, cargo_quantity=?, cargo_volume=?,"
            "sender_address=?, receiver_address=?, status=?, reject_reason=?,"
            "auditor=?, audit_time=? WHERE order_id=?";
        auto stmt=connection.prepare((sql+(expected?" AND status=?":"")).c_str());
        int i = 0;
        stmt.bind_string(i++, order.customer_name);
        stmt.bind_string(i++, order.customer_phone);
        stmt.bind_string(i++, order.customer_address);
        stmt.bind_string(i++, order.cargo_name);
        stmt.bind_int(i++, static_cast<int>(order.cargo_type));
        stmt.bind_double(i++, order.cargo_weight);
        stmt.bind_int(i++, order.cargo_quantity);
        stmt.bind_double(i++, order.cargo_volume);
        stmt.bind_string(i++, order.sender_address);
        stmt.bind_string(i++, order.receiver_address);
        stmt.bind_int(i++, static_cast<int>(order.status));
        stmt.bind_string(i++, order.reject_reason);
        stmt.bind_string(i++, order.auditor);
        stmt.bind_string(i++, order.audit_time);
        stmt.bind_string(i++, order.order_id);
        if(expected)stmt.bind_int(i++,static_cast<int>(*expected));
        return stmt.execute() && stmt.affected_rows() == 1;
    }

public:
    PageResult<Order> list_paged(int page, int page_size,
                                  const std::string& keyword,
                                  std::optional<OrderStatus> filter) override {
        if(page<1 || page>1000000 || page_size<1 || page_size>100)throw std::invalid_argument("Invalid pagination");
        PageResult<Order> result;
        auto lease = ConnectionPool::instance().acquire();

        // 构建 WHERE 子句
        std::string where;
        if (filter) where += "status=" + std::to_string(static_cast<int>(*filter));
        if (!keyword.empty()) {
            if (!where.empty()) where += " AND ";
            where += "(order_id LIKE CONCAT('%',?,'%') OR customer_name LIKE CONCAT('%',?,'%'))";
        }
        std::string where_clause = where.empty() ? "" : " WHERE " + where;

        // 总数
        {
            std::string count_sql = "SELECT COUNT(*) FROM orders" + where_clause;
            auto stmt = lease->prepare(count_sql.c_str());
            int pi = 0;
            if (!keyword.empty()) { stmt.bind_string(pi++, keyword); stmt.bind_string(pi++, keyword); }
            stmt.execute(); stmt.store_result();
            std::vector<std::string> row;
            if (stmt.fetch_row(row)) result.total_count = std::stoi(row[0]);
        }

        result.current_page = page;
        result.total_pages  = std::max(1, (result.total_count + page_size - 1) / page_size);
        int offset = (page - 1) * page_size;

        // 分页数据
        {
            std::string query_sql = "SELECT order_id,customer_name,customer_phone,customer_address,cargo_name,cargo_type,cargo_weight,cargo_quantity,cargo_volume,sender_address,receiver_address,status,reject_reason,creator,create_time,auditor,audit_time FROM orders" + where_clause +
                                    " ORDER BY order_id DESC LIMIT ? OFFSET ?";
            auto stmt = lease->prepare(query_sql.c_str());
            int pi = 0;
            if (!keyword.empty()) { stmt.bind_string(pi++, keyword); stmt.bind_string(pi++, keyword); }
            stmt.bind_int(pi++, page_size);
            stmt.bind_int(pi, offset);

            if (!stmt.execute()) return result;
            if (!stmt.store_result()) return result;

            std::vector<std::string> row;
            while (stmt.fetch_row(row)) {
                if (row.size() >= 17)
                    result.items.push_back(row_to_order(row));
            }
        }
        return result;
    }

    int total_count(const std::string& keyword,
                    std::optional<OrderStatus> filter) override {
        auto lease = ConnectionPool::instance().acquire();

        std::string where;
        if (filter) where += "status=" + std::to_string(static_cast<int>(*filter));
        if (!keyword.empty()) {
            if (!where.empty()) where += " AND ";
            where += "(order_id LIKE CONCAT('%',?,'%') OR customer_name LIKE CONCAT('%',?,'%'))";
        }
        std::string sql = "SELECT COUNT(*) FROM orders" +
                          (where.empty() ? "" : " WHERE " + where);
        auto stmt = lease->prepare(sql.c_str());
        int pi = 0;
        if (!keyword.empty()) { stmt.bind_string(pi++, keyword); stmt.bind_string(pi++, keyword); }
        stmt.execute(); stmt.store_result();
        std::vector<std::string> row;
        return stmt.fetch_row(row) ? std::stoi(row[0]) : 0;
    }

private:
    static Order row_to_order(const std::vector<std::string>& row) {
        Order o;
        o.order_id         = row[0];
        o.customer_name    = row[1];
        o.customer_phone   = row[2];
        o.customer_address = row[3];
        o.cargo_name       = row[4];
        o.cargo_type       = static_cast<CargoType>(row[5].empty() ? 0 : std::stoi(row[5]));
        o.cargo_weight     = row[6].empty() ? 0.0 : std::stod(row[6]);
        o.cargo_quantity   = row[7].empty() ? 0 : std::stoi(row[7]);
        o.cargo_volume     = row[8].empty() ? 0.0 : std::stod(row[8]);
        o.sender_address   = row[9];
        o.receiver_address = row[10];
        o.status           = static_cast<OrderStatus>(row[11].empty() ? 0 : std::stoi(row[11]));
        o.reject_reason    = row[12];
        o.creator          = row[13];
        o.create_time      = row[14];
        o.auditor          = row[15];
        o.audit_time       = row[16];
        return o;
    }
};

} // namespace lms

#endif // LMS_HAS_MYSQL

// ========== 工厂函数 ==========
#ifdef LMS_HAS_MYSQL
std::shared_ptr<lms::IUserRepository> make_user_repo_mysql() {
    static auto instance = std::make_shared<lms::UserRepoMysql>();
    return instance;
}
std::shared_ptr<lms::IOrderRepository> make_order_repo_mysql() {
    static auto instance = std::make_shared<lms::OrderRepoMysql>();
    return instance;
}
#endif
