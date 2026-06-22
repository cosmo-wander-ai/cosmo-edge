// LiveStreamService implementation
// Manages stream viewers lifecycle, heartbeat watchdog, and encoder counting.
// Migrated from flow/stream/StreamViewerMng singleton.

#pragma once

#include <atomic>
#include <memory>
#include <shared_mutex>
#include <thread>
#include <vector>

// Forward declaration — full definition in flow/stream/StreamViewer.h (included in .cc)
namespace cosmo {
class StreamViewer;
using StreamViewerPtr = std::shared_ptr<StreamViewer>;
}  // namespace cosmo

#include "service/media/ILiveStreamService.h"

namespace cosmo::service {

class LiveStreamServiceImpl : public ILiveStreamService {
public:
    LiveStreamServiceImpl();
    ~LiveStreamServiceImpl();

    cosmo::util::ErrorEnum ViewerCreate(const std::string& channelId, const std::string& algCode,
                                        LiveStream::LiveStreamInfo& streamInfo) override;
    bool ViewerDelete(const std::string& channelId, const std::string& algCode) override;
    cosmo::util::ErrorEnum ViewerHeartBeat(const std::string& channelId, const std::string& algCode) override;

    void SetViewCounts(int viewNum) override;

private:
    int ViewerEncoderCountLocked() const;
    void CheckAliveTasks();
    void HeartBeatWatchdog();
    std::vector<cosmo::StreamViewerPtr>::iterator FindViewer(const std::string& channelId,
                                                             const std::string& algCode);

    std::shared_mutex mtx_;
    std::vector<cosmo::StreamViewerPtr> viewers_;
    std::atomic<int> view_counts_{8};

    std::atomic<bool> is_running_{false};
    std::thread watchdog_thread_;
};

}  // namespace cosmo::service
