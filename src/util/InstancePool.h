// InstancePool.h — Template-based instance pool with auto-scaling.

#pragma once

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "util/ErrorCode.h"
#include "util/Log.h"
#include "util/TimingConstants.h"

namespace cosmo {

template <typename T, typename PTR>
class InstancePool {
public:
    explicit InstancePool(const std::string& name, size_t inst_per_tasks = 10, size_t max_inst_count = 4);
    ~InstancePool();
    PTR GetInst(const std::string& atomic_code, const std::string& cfg_path, const std::string& model_path,
                int timeout_ms = 1000);
    void ReturnInst(PTR inst);

    void CreateTask(const std::string& atomic_code, const std::string& cfg_path,
                    const std::string& model_path);
    void DeleteTask();

private:
    size_t InstCount() const;

    mutable std::mutex mtx_;
    std::string name_;
    size_t inst_per_tasks_{10};
    size_t max_inst_count_{1};
    int64_t task_count_{0};
    std::vector<PTR> using_insts_;
    std::vector<PTR> free_insts_;
};

template <typename T, typename PTR>
InstancePool<T, PTR>::InstancePool(const std::string& name, size_t inst_per_tasks, size_t max_inst_count)
    : name_(name), inst_per_tasks_(inst_per_tasks), max_inst_count_(max_inst_count) {
    LOG_INFO("{} inst_per_tasks:{} max_inst_count:{}", name_, inst_per_tasks_, max_inst_count_);
}

template <typename T, typename PTR>
InstancePool<T, PTR>::~InstancePool() {}

template <typename T, typename PTR>
void InstancePool<T, PTR>::CreateTask(const std::string& atomic_code, const std::string& cfg_path,
                                      const std::string& model_path) {
    std::unique_lock<std::mutex> lock(mtx_);
    task_count_ += 1;
    auto inst_count = using_insts_.size() + free_insts_.size();

    // Current instance count exceeds max limit — do not create
    if (inst_count >= max_inst_count_) {
        LOG_INFO("{} [Inst Limit] LimitInstCount:{} NowInstCount:{}", name_, max_inst_count_, inst_count);
        return;
    }
    // Task count below threshold — no need to create instance
    auto limit_tasks = static_cast<int64_t>(inst_count * inst_per_tasks_);
    if (task_count_ <= limit_tasks) {
        return;
    }
    // Unlock before Init(): model loading allocates GPU/NPU memory and reads from disk.
    // Holding the lock during Init() would block GetInst/ReturnInst on every other thread.
    lock.unlock();
    PTR inst = std::make_shared<T>(atomic_code, cfg_path, model_path);
    auto ret = inst->Init();
    lock.lock();

    if (ret != util::ErrorEnum::Success) {
        LOG_WARN("{} Create Inst Failed. ret:{}", name_, ret);
        return;
    }

    // Double-check: another thread may have created an instance while the lock was released.
    if (using_insts_.size() + free_insts_.size() >= max_inst_count_) {
        LOG_INFO("{} [Inst Limit After Init] LimitInstCount:{} NowInstCount:{}", name_, max_inst_count_,
                 using_insts_.size() + free_insts_.size());
        return;
    }

    free_insts_.push_back(inst);
    LOG_INFO("{} inst_per_tasks:{} max_inst_count:{} TaskCount:{} InstCount:{}", name_, inst_per_tasks_,
             max_inst_count_, task_count_, using_insts_.size() + free_insts_.size());
}

template <typename T, typename PTR>
void InstancePool<T, PTR>::DeleteTask() {
    std::unique_lock<std::mutex> lock(mtx_);
    task_count_ -= 1;
    auto inst_count = static_cast<int64_t>(using_insts_.size() + free_insts_.size());
    if (task_count_ < 0) {
        LOG_WARN("{} inst_per_tasks:{} max_inst_count:{} TaskCount:{} InstCount:{}", name_, inst_per_tasks_,
                 max_inst_count_, task_count_, using_insts_.size() + free_insts_.size());
        task_count_ = 0;
        return;
    }

    // Task count exceeds threshold — not enough instances, do not delete
    auto limit_tasks =
        inst_count * static_cast<int64_t>(inst_per_tasks_) - static_cast<int64_t>(inst_per_tasks_);
    if (task_count_ > limit_tasks) {
        return;
    }

    if (free_insts_.empty()) {
        LOG_INFO("{} inst_per_tasks:{} max_inst_count:{} TaskCount:{} InstCount:{}", name_, inst_per_tasks_,
                 max_inst_count_, task_count_, using_insts_.size() + free_insts_.size());
        return;
    }

    free_insts_.pop_back();
    LOG_INFO("{} inst_per_tasks:{} max_inst_count:{} TaskCount:{} InstCount:{}", name_, inst_per_tasks_,
             max_inst_count_, task_count_, using_insts_.size() + free_insts_.size());
    return;
}

template <typename T, typename PTR>
size_t InstancePool<T, PTR>::InstCount() const {
    std::unique_lock<std::mutex> lock(mtx_);
    return free_insts_.size() + using_insts_.size();
}

template <typename T, typename PTR>
PTR InstancePool<T, PTR>::GetInst(const std::string& atomic_code, const std::string& cfg_path,
                                  const std::string& model_path, int timeout_ms) {
    if (InstCount() == 0) {
        LOG_WARN("{} No Inst Need Create", name_);
        CreateTask(atomic_code, cfg_path, model_path);
        if (InstCount() == 0) {
            LOG_ERRO("{} inst_per_tasks:{} max_inst_count:{} TaskCount:{} InstCount:{}", name_,
                     inst_per_tasks_, max_inst_count_, task_count_, using_insts_.size() + free_insts_.size());
            return nullptr;
        }
    }
    int count = 1;
    if (timeout_ms < 0) {
        count = 100000000;
    } else if (timeout_ms > 10) {
        count = timeout_ms / 10;
    }

    std::unique_lock<std::mutex> lock(mtx_);
    int index = 0;
    while (index++ < count) {
        if (free_insts_.empty()) {
            lock.unlock();
            std::this_thread::sleep_for(timing::kFastPollInterval);
            lock.lock();
            continue;
        }
        auto inst = free_insts_.back();
        free_insts_.pop_back();
        using_insts_.push_back(inst);
        return inst;
    }

    return nullptr;
}

template <typename T, typename PTR>
void InstancePool<T, PTR>::ReturnInst(PTR inst) {
    if (!inst) {
        return;
    }
    std::unique_lock<std::mutex> lock(mtx_);
    auto iter = std::find_if(using_insts_.begin(), using_insts_.end(),
                             [&](const PTR& element) { return inst == element; });
    if (iter != using_insts_.end()) {
        using_insts_.erase(iter);
        free_insts_.push_back(inst);
    } else {
        LOG_WARN("{} Inst Not Found", name_);
    }
}

}  // namespace cosmo
