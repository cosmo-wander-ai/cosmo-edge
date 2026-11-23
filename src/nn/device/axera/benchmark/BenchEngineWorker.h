#pragma once

#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace cosmo::bench_ax {
inline bool BenchmarkEngineWorkerEnabled() noexcept {
    const char* p = std::getenv("COSMO_AXERA_BENCH_CENTRAL_WORKER");
    return p && std::strcmp(p, "1") == 0;
}
class BenchEngineWorker final {
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::function<void()>> detector_, classifier_;
    bool stopping_{false}, next_classifier_{false};
    std::thread thread_;
    static BenchEngineWorker*& CurrentWorker() noexcept {
        static thread_local BenchEngineWorker* worker = nullptr;
        return worker;
    }

public:
    BenchEngineWorker()
        : thread_([this] {
              CurrentWorker() = this;
              for (;;) {
                  std::function<void()> task;
                  {
                      std::unique_lock<std::mutex> lock(mutex_);
                      changed_.wait(
                          lock, [this] { return stopping_ || !detector_.empty() || !classifier_.empty(); });
                      if (stopping_ && detector_.empty() && classifier_.empty())
                          break;
                      const bool classifier = detector_.empty() || (!classifier_.empty() && next_classifier_);
                      auto& queue           = classifier ? classifier_ : detector_;
                      task                  = std::move(queue.front());
                      queue.pop_front();
                      next_classifier_ = !classifier;
                  }
                  task();
              }
              CurrentWorker() = nullptr;
          }) {}
    BenchEngineWorker(const BenchEngineWorker&)            = delete;
    BenchEngineWorker& operator=(const BenchEngineWorker&) = delete;
    ~BenchEngineWorker() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        changed_.notify_all();
        if (thread_.joinable())
            thread_.join();
    }
    static bool OnWorkerThread() noexcept {
        return CurrentWorker() != nullptr;
    }
    template <class F>
    auto Submit(bool classifier, F&& fn) -> std::future<std::invoke_result_t<F>> {
        using R = std::invoke_result_t<F>;
        static_assert(!std::is_reference_v<R>, "worker results must own their lifetime");
        auto task   = std::make_shared<std::packaged_task<R()>>(std::forward<F>(fn));
        auto result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_)
                throw std::runtime_error("benchmark engine worker is stopping");
            (classifier ? classifier_ : detector_).emplace_back([task] { (*task)(); });
        }
        changed_.notify_one();
        return result;
    }
    template <class F>
    auto Invoke(bool classifier, F&& fn) -> std::invoke_result_t<F> {
        if (CurrentWorker() == this)
            return std::forward<F>(fn)();
        return Submit(classifier, std::forward<F>(fn)).get();
    }
};
inline BenchEngineWorker& BenchmarkEngineWorker() {
    static BenchEngineWorker worker;
    return worker;
}
}  // namespace cosmo::bench_ax
