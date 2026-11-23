#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace cosmo {
// Access is serialized by the owning channel mutex. The first supported
// mixed route is deliberately limited to the already admitted face/person
// scene pair. Every task still needs its own native model authorization.
class NativeFaceReservation {
public:
    static int PairFamily(const std::string& task) {
        const auto split = task.rfind('_');
        if (split == std::string::npos || split == 0)
            return 0;
        const auto scene = task.substr(split + 1);
        if (scene == "60581" || scene == "74470")
            return 1;
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
        if (scene == "930927510" || scene == "930007463")
            return 2;
#endif
        return 0;
    }
    static bool IsSupportedPairTask(const std::string& task) {
        return PairFamily(task) != 0;
    }
    static bool CompatiblePairTasks(const std::string& a, const std::string& b) {
        const auto family = PairFamily(a);
        return family && family == PairFamily(b) && a.substr(a.rfind('_') + 1) != b.substr(b.rfind('_') + 1);
    }
    bool CanAttachTask(const std::string& task) const {
        if (!Active())
            return true;
        return reservations_.size() == 1 && CompatiblePairTasks(task, reservations_[0].task);
    }
    bool CanDetachTask(const std::string& task) const {
        if (!Active())
            return true;
        return IsSupportedPairTask(task) && std::none_of(reservations_.begin(), reservations_.end(),
                                                         [&](const auto& item) { return item.task == task; });
    }
    bool CanJoin(const std::string& task, bool source_is_1080p) const {
        return source_is_1080p && Active() && CanAttachTask(task);
    }
    bool Reserve(bool started, const std::vector<std::string>& tasks,
                 const std::shared_ptr<const void>& token, const std::string& expected_task,
                 const std::string& camera_task = {}) {
        if (!token || expected_task.empty() || expected_task == camera_task ||
            std::count(tasks.begin(), tasks.end(), expected_task) != 1 || reservations_.size() >= 2 ||
            (!Active() && started) || (Active() && (!started || !CanAttachTask(expected_task))))
            return false;
        // tasks contains all configured bindings, including stopped tasks. A
        // stopped binding owns no frame queue or reservation and must not block
        // the active task. Active native joins remain restricted by CanAttachTask.
        if (Active() && camera_task_ != camera_task)
            return false;
        if (std::any_of(reservations_.begin(), reservations_.end(), [&](const auto& item) {
                return SameToken(item.token, token) || item.task == expected_task;
            }))
            return false;
        camera_task_ = camera_task;
        reservations_.push_back({token, expected_task});
        return true;
    }
    bool Release(bool started, const std::shared_ptr<const void>& token) {
        const auto it = std::find_if(reservations_.begin(), reservations_.end(),
                                     [&](const auto& item) { return SameToken(item.token, token); });
        if (it == reservations_.end() || (reservations_.size() == 1 && started))
            return false;
        reservations_.erase(it);
        if (reservations_.empty())
            camera_task_.clear();
        return true;
    }
    bool Active() const {
        return !reservations_.empty();
    }
    bool HasOther(const std::shared_ptr<const void>& token) const {
        return reservations_.size() > 1 &&
               std::any_of(reservations_.begin(), reservations_.end(),
                           [&](const auto& item) { return SameToken(item.token, token); });
    }
    bool AllowsTask(const std::string& task) const {
        return !Active() || std::any_of(reservations_.begin(), reservations_.end(),
                                        [&](const auto& item) { return item.task == task; });
    }
    bool IsCameraMetadataTask(const std::string& task) const {
        return Active() && !camera_task_.empty() && task == camera_task_;
    }
    bool Exclusive(const std::vector<std::string>& tasks) const {
        if (!Active())
            return true;
        for (const auto& item : reservations_)
            if (std::count(tasks.begin(), tasks.end(), item.task) != 1)
                return false;
        // Unreserved configured tasks are inactive: queue registration is
        // guarded independently by AllowsTask, and joins require a reservation.
        return true;
    }

private:
    struct Entry {
        std::shared_ptr<const void> token;
        std::string task;
    };
    static bool SameToken(const std::shared_ptr<const void>& a, const std::shared_ptr<const void>& b) {
        return a && b && a.get() == b.get() && !a.owner_before(b) && !b.owner_before(a);
    }
    std::vector<Entry> reservations_;
    std::string camera_task_;
};
}  // namespace cosmo
