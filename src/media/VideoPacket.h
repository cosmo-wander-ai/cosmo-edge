#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "media/VideoCodecType.h"

namespace cosmo::media {

struct VideoPacket {
    VideoPacket() = default;

    ~VideoPacket() = default;

    [[nodiscard]] int64_t GetSequence() const;

    [[nodiscard]] int64_t GetTimestamp() const;

    [[nodiscard]] int64_t GetTimestampEpoch() const;

    [[nodiscard]] uint8_t* GetData();
    [[nodiscard]] size_t GetSize() const;

    [[nodiscard]] VideoCodecType GetType() const;

    [[nodiscard]] size_t GetWidth() const;
    [[nodiscard]] size_t GetHeight() const;

    [[nodiscard]] float GetFPS() const;

    [[nodiscard]] bool IsIFrame() const;
    std::vector<uint8_t> data;
    int64_t pts             = 0;
    int64_t timestamp       = 0;
    int64_t timestamp_epoch = 0;

    std::chrono::steady_clock::time_point time_point;

    int64_t stream_idx = 0;

    int64_t index   = 0;
    int64_t abs_idx = 0;

    bool is_i_frame = false;

    VideoCodecType codec_type = VideoCodecType::kH264;

    size_t width  = 0;
    size_t height = 0;

    float fps = 0.f;
};

}  // namespace cosmo::media

namespace cosmo::media {
using VideoPacketPtr = std::shared_ptr<VideoPacket>;
}  // namespace cosmo::media

// Legacy global alias — kept for backward compatibility.
// TODO(refactor): Remove after downstream modules migrate to cosmo::media::VideoPacketPtr.
using VideoPacketPtr = std::shared_ptr<cosmo::media::VideoPacket>;
