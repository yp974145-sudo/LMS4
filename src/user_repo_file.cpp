#include "lms/repository.h"
#include "lms/crypto.h"
#include "lms/file_records.h"
#include <limits>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <cstring>
#include <algorithm>

namespace lms {

// ========== 文件版用户仓库 ==========
class UserRepoFile : public IUserRepository {
    inline static std::mutex file_mutex_;
    std::filesystem::path filename_;

    std::vector<User> read_all() const { return file_records::read_users(filename_); }

    void write_all(const std::vector<User>& users) const {
        file_storage::save(filename_, 'U', file_records::encode_users(users));
    }

public:
    explicit UserRepoFile(const std::string& directory) : filename_(std::filesystem::path(directory)/"user.dat") { ensure_defaults(); }

    std::optional<User> verify_login(const std::string& username,
                                      const std::string& password) override {
        std::lock_guard<std::mutex> guard(file_mutex_);
        auto users = read_all();
        for (const auto& u : users) {
            if (u.username == username && u.password_hash == password) {
                if (u.status == UserStatus::Locked) return std::nullopt;
                return u;
            }
        }
        return std::nullopt;
    }

    std::optional<User> find_by_username(const std::string& username) override {
        std::lock_guard<std::mutex> guard(file_mutex_);
        auto users = read_all();
        for (const auto& u : users) {
            if (u.username == username) return u;
        }
        return std::nullopt;
    }

    std::optional<User> create(const User& user) override {
        std::lock_guard<std::mutex> guard(file_mutex_);
        auto users = read_all();
        // 查重
        if (std::any_of(users.begin(), users.end(),
                [&](const User& u) { return u.username == user.username; }))
            return std::nullopt;

        User nu = user;
        int maximum = 0;
        for (const auto& u : users) maximum = std::max(maximum,u.id);
        if (maximum == std::numeric_limits<int>::max()) throw file_storage::StorageError("User id exhausted");
        nu.id = maximum + 1;
        users.push_back(nu);
        write_all(users);
        return nu;
    }

    PageResult<User> list_paged(int page, int page_size,
                                 const std::string& keyword) override {
        std::lock_guard<std::mutex> guard(file_mutex_);
        if (page < 1 || page > 1000000 || page_size < 1 || page_size > 100)
            throw std::invalid_argument("Invalid pagination");
        auto users = read_all();
        PageResult<User> result;
        result.total_count = static_cast<int>(users.size());

        // 过滤
        std::vector<User> filtered;
        for (const auto& u : users) {
            if (keyword.empty() || u.username.find(keyword) != std::string::npos)
                filtered.push_back(u);
        }

        result.total_count = static_cast<int>(filtered.size());
        result.current_page = page;
        result.total_pages  = std::max(1, (result.total_count + page_size - 1) / page_size);

        int start = (page - 1) * page_size;
        int end   = std::min(start + page_size, result.total_count);
        for (int i = start; i < end; ++i)
            result.items.push_back(filtered[i]);

        return result;
    }

    bool reset_password(const std::string& username,
                         const std::string& new_pwd) override {
        std::lock_guard<std::mutex> guard(file_mutex_);
        auto users = read_all();
        for (auto& u : users) {
            if (u.username == username) {
                u.password_hash = new_pwd;
                write_all(users);
                return true;
            }
        }
        return false;
    }

    int total_count(const std::string& keyword) override {
        std::lock_guard<std::mutex> guard(file_mutex_);
        auto users = read_all();
        if (keyword.empty()) return static_cast<int>(users.size());
        return static_cast<int>(std::count_if(users.begin(), users.end(),
            [&](const User& u) { return u.username.find(keyword) != std::string::npos; }));
    }

private:
    void ensure_defaults() {
        std::lock_guard<std::mutex> guard(file_mutex_);
        if (file_storage::exists(filename_)) { read_all(); return; }
        if (file_storage::exists(file_storage::backup_path(filename_)))
            throw file_storage::StorageError("User snapshot missing; explicit recovery from .bak required");

        // 首次运行：写入默认账号
        // SHA-256 预计算哈希值
        std::vector<User> defaults = {
            {1, "admin",    crypto::sha256("admin123"), UserRole::Admin,       UserStatus::Active},
            {2, "storage",  crypto::sha256("storage123"), UserRole::Storage,     UserStatus::Active},
            {3, "dispatch", crypto::sha256("dispatch123"), UserRole::Dispatcher,  UserStatus::Active},
            {4, "customer", crypto::sha256("customer123"), UserRole::CustomerSvc, UserStatus::Active},
        };
        write_all(defaults);
    }
};

} // namespace lms

// ========== 工厂函数 ==========
std::shared_ptr<lms::IUserRepository> make_user_repo_file(const std::string& directory) {
    return std::make_shared<lms::UserRepoFile>(directory);
}
