#include "service/event/AlarmMediaActivity.h"

#include <mutex>
#include <unordered_map>
#include <utility>

namespace cosmo::service {

struct AlarmMediaLease::State {
    struct Entry {
        size_t recordings{0};
        bool deleting{false};
    };
    std::mutex mutex;
    std::unordered_map<std::string, Entry> events;
};

namespace {
    std::shared_ptr<AlarmMediaLease::State> ActivityState() {
        static auto state = std::make_shared<AlarmMediaLease::State>();
        return state;
    }
}  // namespace

AlarmMediaLease::AlarmMediaLease(std::shared_ptr<State> state, std::string id, bool deleting)
    : state_(std::move(state)), id_(std::move(id)), deleting_(deleting) {}

AlarmMediaLease::~AlarmMediaLease() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto it = state_->events.find(id_);
    if (it == state_->events.end())
        return;
    if (deleting_)
        it->second.deleting = false;
    else
        --it->second.recordings;
    if (!it->second.deleting && it->second.recordings == 0)
        state_->events.erase(it);
}

std::unique_ptr<AlarmMediaLease> AlarmMediaLease::StartRecording(const std::string& id) {
    auto state = ActivityState();
    std::lock_guard<std::mutex> lock(state->mutex);
    auto& entry = state->events[id];
    if (entry.deleting)
        return nullptr;
    auto lease = std::unique_ptr<AlarmMediaLease>(new AlarmMediaLease(state, id, false));
    ++entry.recordings;
    return lease;
}

std::unique_ptr<AlarmMediaLease> AlarmMediaLease::TryDelete(const std::string& id) {
    auto state = ActivityState();
    std::lock_guard<std::mutex> lock(state->mutex);
    auto& entry = state->events[id];
    if (entry.deleting || entry.recordings != 0)
        return nullptr;
    auto lease     = std::unique_ptr<AlarmMediaLease>(new AlarmMediaLease(state, id, true));
    entry.deleting = true;
    return lease;
}

}  // namespace cosmo::service
