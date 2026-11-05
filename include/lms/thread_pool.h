#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <type_traits>

// ============================================
// 通用线程池 —— 字节跳动高频手撕题
//
// 面经常问（2025-2026 字节出现 8+ 次）：
//   Q: "手写一个线程池"
//   Q: "线程数量怎么定？"
//   Q: "任务队列满了怎么办？"
//
// 核心设计：
//   1. 固定数量工作线程（std::thread::hardware_concurrency()）
//   2. 任务队列（std::queue + condition_variable）
//   3. 支持 submit() 返回 std::future 获取异步结果
//   4. RAII 析构自动停止所有线程
//
// 线程数公式（面试要能讲）：
//   CPU密集：n_cores 个线程（多了上下文切换开销大于收益）
//   I/O密集：n_cores * 2 个线程（等I/O时可以让出CPU）
//   混合型：实测调优
// ============================================

namespace lms {

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = 0)
        : stop_(false)
    {
        if (num_threads == 0)
            num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;  // 兜底

        workers_.reserve(num_threads);
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock lock(mutex_);
                        // ★ 条件变量：等任务到来或线程池停止
                        cv_.wait(lock, [this] {
                            return stop_ || !tasks_.empty();
                        });
                        if (stop_ && tasks_.empty()) return;
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();  // 执行任务（不持锁）
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();  // 唤醒所有等待的线程
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
    }

    // 提交任务，返回 future 获取结果
    // 用法：auto f = pool.submit([](int a, int b) { return a+b; }, 1, 2);
    //       int result = f.get();  // 阻塞直到任务完成
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result_t<F, Args...>>
    {
        using return_type = typename std::invoke_result_t<F, Args...>;

        // packaged_task 把可调用对象包装成 future
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<return_type> result = task->get_future();

        {
            std::lock_guard lock(mutex_);
            if (stop_) throw std::runtime_error("ThreadPool: submit on stopped pool");
            tasks_.emplace([task]() { (*task)(); });
        }
        cv_.notify_one();  // 唤醒一个工作线程
        return result;
    }

    size_t worker_count() const { return workers_.size(); }
    size_t pending_tasks() const {
        std::lock_guard lock(mutex_);
        return tasks_.size();
    }

    // 禁止拷贝
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_;
};

} // namespace lms
