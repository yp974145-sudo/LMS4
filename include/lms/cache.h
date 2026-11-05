#pragma once

#include <string>
#include <optional>
#include <memory>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <fstream>
#include <sstream>

// ============================================
// 缓存接口 —— 为 Redis 集成预留
//
// 设计意图：
//   ICache 抽象缓存操作，生产环境可接入 Redis，
//   本地开发/测试用 FileCache 兜底。
//   这种 "接口隔离 + 降级兜底" 的模式是后端面试的高频话题。
//
// Redis 集成路径（面试可讲）：
//   1. 安装 hiredis 或 redis-plus-plus
//   2. 实现 RedisCache : public ICache
//   3. connect() 连接 Redis，set()/get() 映射到 SET/GET 命令
//   4. 设置 TTL 实现自动过期
//   5. main.cpp 中编译期选择 RedisCache 或 FileCache
// ============================================

namespace lms::cache {

// ---- 缓存接口 ----
class ICache {
public:
    virtual ~ICache() = default;

    // 存入键值对（默认过期 300 秒）
    virtual void set(const std::string& key, const std::string& value,
                     int ttl_sec = 300) = 0;

    // 读取（过期返回 nullopt）
    virtual std::optional<std::string> get(const std::string& key) = 0;

    // 删除
    virtual void del(const std::string& key) = 0;

    // 是否存在且未过期
    virtual bool exists(const std::string& key) = 0;

    // 清空全部缓存
    virtual void clear() = 0;
};

// ---- 文件缓存实现（零依赖，开发/测试用） ----
//
// 存储格式（lms_cache.dat）：每行一条记录
//   key \t value \t expire_timestamp
//
// 性能特征：
//   - 写入：追加写 + 内存索引（O(1) 查找）
//   - 启动时从文件恢复索引
//   - 不适用高并发生产环境，但能跑通完整缓存语义
//
class FileCache : public ICache {
public:
    explicit FileCache(const std::string& filepath = "lms_cache.dat")
        : filepath_(filepath) {
        load_from_disk();
    }

    ~FileCache() override {
        flush_to_disk();
    }

    void set(const std::string& key, const std::string& value,
             int ttl_sec = 300) override {
        std::unique_lock lock(mutex_);
        auto expires = now_sec() + ttl_sec;
        entries_[key] = {value, expires};
        dirty_ = true;
    }

    std::optional<std::string> get(const std::string& key) override {
        std::shared_lock lock(mutex_);
        auto it = entries_.find(key);
        if (it == entries_.end()) return std::nullopt;
        if (it->second.expires < now_sec()) {
            // 惰性删除：读到过期时清理
            lock.unlock();
            std::unique_lock wlock(mutex_);
            entries_.erase(key);
            return std::nullopt;
        }
        return it->second.value;
    }

    void del(const std::string& key) override {
        std::unique_lock lock(mutex_);
        entries_.erase(key);
        dirty_ = true;
    }

    bool exists(const std::string& key) override {
        return get(key).has_value();
    }

    void clear() override {
        std::unique_lock lock(mutex_);
        entries_.clear();
        dirty_ = true;
    }

    // 获取缓存条目数（调试用）
    size_t size() const { return entries_.size(); }

private:
    struct Entry {
        std::string value;
        int64_t     expires;  // unix 时间戳
    };

    std::string filepath_;
    std::unordered_map<std::string, Entry> entries_;
    mutable std::shared_mutex mutex_;
    bool dirty_ = false;

    static int64_t now_sec() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    void load_from_disk() {
        std::ifstream f(filepath_);
        if (!f.is_open()) return;
        std::string line;
        int64_t now = now_sec();
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            std::string key, value, expire_str;
            if (std::getline(ss, key, '\t') &&
                std::getline(ss, value, '\t') &&
                std::getline(ss, expire_str)) {
                int64_t exp = std::stoll(expire_str);
                if (exp > now) {  // 加载时过滤已过期
                    entries_[key] = {value, exp};
                }
            }
        }
    }

    void flush_to_disk() {
        if (!dirty_) return;
        std::ofstream f(filepath_, std::ios::trunc);
        if (!f.is_open()) return;
        int64_t now = now_sec();
        for (auto& [k, v] : entries_) {
            if (v.expires > now) {
                f << k << '\t' << v.value << '\t' << v.expires << '\n';
            }
        }
        dirty_ = false;
    }
};

// ---- Redis 缓存实现（面试可讲的设计桩） ----
//
// 当前为架构占位，展示接口设计的扩展性。
// 接入 Redis 只需实现以下几个方法：
//
//   class RedisCache : public ICache {
//       redisContext* ctx_;
//   public:
//       void set(key, value, ttl) override {
//           redisCommand(ctx_, "SETEX %s %d %s", key, ttl, value);
//       }
//       optional<string> get(key) override {
//           auto* reply = redisCommand(ctx_, "GET %s", key);
//           // ... 处理 reply
//       }
//       // ...
//   };
//
// 在 main.cpp 中：
//   #ifdef LMS_HAS_REDIS
//       cache = std::make_shared<RedisCache>("127.0.0.1", 6379);
//   #else
//       cache = std::make_shared<FileCache>();
//   #endif
//
// 与 MySQL 双后端的模式完全一致 —— 这个一致性本身就是面试论点。

// ============================================
// LRU 缓存淘汰 —— 腾讯必考手撕题（面经出现 32+ 次）
//
// 核心结构：双向链表 + 哈希表
//   哈希表 → O(1) 查找
//   双向链表 → O(1) 移动节点到头部
//
// 面试会被问到：
//   Q: "为什么用双向链表而不是单向？"
//   A: 删除中间节点需要前驱指针。单向链表删中间节点是 O(n)，
//      双向是 O(1)。LRU 每次访问都要把节点移到头部——本质是
//      先删除再头插——两步都必须 O(1) 才能保证整体 O(1)。
//
//   Q: "为什么不用 priority_queue？"
//   A: priority_queue 按时间戳排序，但 LRU 的"访问"会改变时间戳，
//      priority_queue 不支持动态更新优先级。
// ============================================

template<typename K, typename V>
class LRUCache {
    struct Node {
        K key;
        V value;
        Node* prev = nullptr;
        Node* next = nullptr;
    };

    size_t capacity_;
    std::unordered_map<K, Node*> map_;
    Node* head_;  // 哨兵头（最近使用）
    Node* tail_;  // 哨兵尾（最久未使用）

    // 从链表中移除节点（不释放内存）
    void detach(Node* node) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
    }

    // 插入到头部（标记为最近使用）
    void attach_to_head(Node* node) {
        node->next = head_->next;
        node->prev = head_;
        head_->next->prev = node;
        head_->next = node;
    }

public:
    explicit LRUCache(size_t capacity) : capacity_(capacity) {
        head_ = new Node{};
        tail_ = new Node{};
        head_->next = tail_;
        tail_->prev = head_;
    }

    ~LRUCache() {
        for (auto& [_, node] : map_) delete node;
        delete head_;
        delete tail_;
    }

    std::optional<V> get(const K& key) {
        auto it = map_.find(key);
        if (it == map_.end()) return std::nullopt;
        // ★ 移到头部 = 标记为最近使用
        detach(it->second);
        attach_to_head(it->second);
        return it->second->value;
    }

    void put(const K& key, const V& value) {
        auto it = map_.find(key);
        if (it != map_.end()) {
            // 更新已有 key
            it->second->value = value;
            detach(it->second);
            attach_to_head(it->second);
        } else {
            // 新 key
            if (map_.size() >= capacity_) {
                // ★ 淘汰最久未使用（尾部前一个节点）
                Node* lru = tail_->prev;
                detach(lru);
                map_.erase(lru->key);
                delete lru;
            }
            Node* node = new Node{key, value};
            attach_to_head(node);
            map_[key] = node;
        }
    }

    size_t size() const { return map_.size(); }
};

} // namespace lms::cache
