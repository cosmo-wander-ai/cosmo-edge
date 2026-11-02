#pragma once

#include <memory>
#include <string>

namespace cosmo::service {

// A recording lease lives through MP4 and metadata finalization. Deletion and
// recording registration are mutually exclusive for the same event ID.
class AlarmMediaLease {
public:
    struct State;

    ~AlarmMediaLease();
    AlarmMediaLease(const AlarmMediaLease&)            = delete;
    AlarmMediaLease& operator=(const AlarmMediaLease&) = delete;

    static std::unique_ptr<AlarmMediaLease> StartRecording(const std::string& id);
    static std::unique_ptr<AlarmMediaLease> TryDelete(const std::string& id);

private:
    AlarmMediaLease(std::shared_ptr<State> state, std::string id, bool deleting);

    std::shared_ptr<State> state_;
    std::string id_;
    bool deleting_;
};

}  // namespace cosmo::service
