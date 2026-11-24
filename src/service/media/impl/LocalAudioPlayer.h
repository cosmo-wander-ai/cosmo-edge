#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "util/BoundedProcess.h"

namespace cosmo::service {

class LocalAudioPlayer {
public:
    struct Clip {
        std::filesystem::path path;
        std::chrono::milliseconds duration;
    };
    using ClipPtr = std::shared_ptr<const Clip>;
    using Probe   = std::function<std::string()>;
    using Runner  = std::function<util::BoundedProcessResult(const std::vector<std::string>&,
                                                             std::chrono::steady_clock::time_point,
                                                             const std::function<bool()>&)>;

    LocalAudioPlayer();
    LocalAudioPlayer(std::filesystem::path cache_directory, Probe probe, Runner runner);
    ~LocalAudioPlayer();

    std::string UnavailableReason() const;
    // The settings owner serializes Prepare and publishing the returned clip with SetClip.
    // The source is the resolved local library file. Preparation never changes live playback.
    ClipPtr Prepare(const std::string& file_id, const std::filesystem::path& source, std::string& reason);
    void SetClip(ClipPtr clip);
    void Enqueue();
    void Stop();

private:
    void Run();
    void PruneCache(const std::filesystem::path& requested);

    const std::filesystem::path cache_directory_;
    const Probe probe_;
    const Runner runner_;
    std::vector<std::weak_ptr<const Clip>> prepared_clips_;
    std::mutex mutex_;
    std::mutex stop_mutex_;
    std::condition_variable changed_;
    std::thread worker_;
    ClipPtr selected_;
    ClipPtr playing_;
    std::uint64_t pending_{0};
    std::uint64_t generation_{0};
    bool stopped_{false};
};

}  // namespace cosmo::service
