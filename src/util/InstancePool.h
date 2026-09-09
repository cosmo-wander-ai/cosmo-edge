// InstancePool.h — Template-based instance pool with auto-scaling.

#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
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
    enum class CreatePurpose { kPrewarm, kAcquire };
    enum class CreateStatus { kCreated, kSkipped, kInitFailed };

    struct CreateResult {
        CreateStatus status;
        PTR inst;
    };

    struct CreationReservation {
        explicit CreationReservation(InstancePool& owner) : pool(owner) {}
        ~CreationReservation() {
            if (active) {
                std::lock_guard<std::mutex> lock(pool.mtx_);
                --pool.creating_count_;
            }
        }
        CreationReservation(const CreationReservation&)            = delete;
        CreationReservation& operator=(const CreationReservation&) = delete;

        InstancePool& pool;
        bool active{false};
    };

    size_t DesiredInstCountLocked() const;
    CreateResult TryCreateInst(const std::string& atomic_code, const std::string& cfg_path,
                               const std::string& model_path, CreatePurpose purpose);
    void TrimFreeInstsLocked(std::vector<PTR>& retired);
    void LogState() const;

    mutable std::mutex mtx_;
    std::string name_;
    size_t inst_per_tasks_{10};
    size_t max_inst_count_{1};
    int64_t task_count_{0};
    size_t creating_count_{0};
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
    {
        std::lock_guard<std::mutex> lock(mtx_);
        ++task_count_;
    }
    TryCreateInst(atomic_code, cfg_path, model_path, CreatePurpose::kPrewarm);
    LogState();
}

template <typename T, typename PTR>
void InstancePool<T, PTR>::DeleteTask() {
    std::vector<PTR> retired;
    bool excess_delete = false;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (task_count_ > 0) {
            --task_count_;
        } else {
            excess_delete = true;
        }
        TrimFreeInstsLocked(retired);
    }
    retired.clear();
    if (excess_delete) {
        LOG_WARN("{} DeleteTask without a registered task", name_);
    }
    LogState();
}

template <typename T, typename PTR>
size_t InstancePool<T, PTR>::DesiredInstCountLocked() const {
    if (task_count_ == 0) {
        return 0;
    }
    if (inst_per_tasks_ == 0) {
        return max_inst_count_;
    }
    const auto tasks   = static_cast<uint64_t>(task_count_);
    const auto desired = tasks / inst_per_tasks_ + (tasks % inst_per_tasks_ != 0);
    return static_cast<size_t>(std::min<uint64_t>(max_inst_count_, desired));
}

template <typename T, typename PTR>
typename InstancePool<T, PTR>::CreateResult InstancePool<T, PTR>::TryCreateInst(
    const std::string& atomic_code, const std::string& cfg_path, const std::string& model_path,
    CreatePurpose purpose) {
    // Unpublished models are destroyed before their reservation is released.
    CreationReservation reservation(*this);
    PTR inst;
    std::vector<PTR> retired;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        const auto reserved = using_insts_.size() + free_insts_.size() + creating_count_;
        if (reserved >= max_inst_count_) {
            return {CreateStatus::kSkipped, nullptr};
        }
        if (purpose == CreatePurpose::kPrewarm) {
            if (reserved >= DesiredInstCountLocked()) {
                return {CreateStatus::kSkipped, nullptr};
            }
        } else if (reserved != 0) {
            // Acquisitions recover an empty pool; registrations control scaling.
            return {CreateStatus::kSkipped, nullptr};
        }
        ++creating_count_;
        reservation.active = true;
    }

    inst           = std::make_shared<T>(atomic_code, cfg_path, model_path);
    const auto ret = inst->Init();
    if (ret != util::ErrorEnum::Success) {
        LOG_WARN("{} Create Inst Failed. ret:{}", name_, ret);
        return {CreateStatus::kInitFailed, nullptr};
    }

    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (purpose == CreatePurpose::kPrewarm) {
            // Prefer a completed model over other, still unverified creations.
            if (using_insts_.size() + free_insts_.size() >= DesiredInstCountLocked()) {
                return {CreateStatus::kSkipped, nullptr};
            }
            free_insts_.push_back(inst);
        } else {
            // Complete the requested borrow even if its registration has ended.
            // Allocate retirement storage before publishing to keep failure atomic.
            retired.reserve(free_insts_.size());
            using_insts_.push_back(inst);
            TrimFreeInstsLocked(retired);
        }
        --creating_count_;
        reservation.active = false;
    }
    // No potentially throwing logging after publishing a borrow to its caller.
    return {CreateStatus::kCreated, purpose == CreatePurpose::kAcquire ? std::move(inst) : PTR{}};
}

template <typename T, typename PTR>
void InstancePool<T, PTR>::TrimFreeInstsLocked(std::vector<PTR>& retired) {
    const auto desired = DesiredInstCountLocked();
    const auto keep    = desired > using_insts_.size() ? desired - using_insts_.size() : 0;
    if (free_insts_.size() <= keep) {
        return;
    }
    // Each caller supplies an empty retirement vector.
    if (keep == 0) {
        retired.swap(free_insts_);
        return;
    }
    retired.reserve(free_insts_.size() - keep);
    while (free_insts_.size() > keep) {
        retired.push_back(std::move(free_insts_.back()));
        free_insts_.pop_back();
    }
}

template <typename T, typename PTR>
void InstancePool<T, PTR>::LogState() const {
    int64_t tasks;
    size_t instances;
    size_t creating;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        tasks     = task_count_;
        instances = using_insts_.size() + free_insts_.size();
        creating  = creating_count_;
    }
    LOG_INFO("{} inst_per_tasks:{} max_inst_count:{} TaskCount:{} InstCount:{} CreatingCount:{}", name_,
             inst_per_tasks_, max_inst_count_, tasks, instances, creating);
}

template <typename T, typename PTR>
PTR InstancePool<T, PTR>::GetInst(const std::string& atomic_code, const std::string& cfg_path,
                                  const std::string& model_path, int timeout_ms) {
    if (max_inst_count_ == 0) {
        return nullptr;
    }
    int count = 1;
    if (timeout_ms < 0) {
        count = 100000000;
    } else if (timeout_ms > 10) {
        count = timeout_ms / 10;
    }

    int index = 0;
    while (index++ < count) {
        bool needs_creation = false;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (!free_insts_.empty()) {
                auto inst = free_insts_.back();
                using_insts_.push_back(inst);
                free_insts_.pop_back();
                return inst;
            }
            needs_creation = using_insts_.empty() && creating_count_ == 0;
        }
        if (needs_creation) {
            auto result = TryCreateInst(atomic_code, cfg_path, model_path, CreatePurpose::kAcquire);
            if (result.status == CreateStatus::kCreated) {
                return std::move(result.inst);
            }
            if (result.status == CreateStatus::kInitFailed) {
                return nullptr;
            }
        }
        std::this_thread::sleep_for(timing::kFastPollInterval);
    }

    return nullptr;
}

template <typename T, typename PTR>
void InstancePool<T, PTR>::ReturnInst(PTR inst) {
    if (!inst) {
        return;
    }
    std::vector<PTR> retired;
    bool found    = false;
    bool released = false;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        const auto iter = std::find(using_insts_.begin(), using_insts_.end(), inst);
        if (iter != using_insts_.end()) {
            found                = true;
            const auto remaining = using_insts_.size() + free_insts_.size() - 1;
            if (remaining < DesiredInstCountLocked()) {
                free_insts_.push_back(inst);
            } else {
                retired.reserve(free_insts_.size());
                released = true;
            }
            using_insts_.erase(iter);
            TrimFreeInstsLocked(retired);
        }
    }
    if (!found) {
        LOG_WARN("{} Inst Not Found", name_);
    } else if (released || !retired.empty()) {
        inst.reset();
        retired.clear();
        LogState();
    }
}

}  // namespace cosmo
