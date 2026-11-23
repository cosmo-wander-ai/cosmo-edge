#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "media/VideoCodecType.h"

namespace cosmo::media {

// Conservative admission for seamless replay: require an in-band, complete
// parameterized IDR and reject any later parameter-set change. Streams that
// cannot prove this retain the ordinary reconnect/recording-boundary path.
class LocalLoopCodecGuard {
public:
    void Reset();
    bool Observe(VideoCodecType codec, const uint8_t* data, size_t size, bool boundary);
    static bool UnchangedExtradata(const std::vector<uint8_t>* expected, const uint8_t* data, size_t size);
    bool Eligible() const {
        return eligible_;
    }

private:
    VideoCodecType codec_{VideoCodecType::kInvalid};
    std::array<std::vector<uint8_t>, 3> parameters_;
    bool eligible_{true};
    bool initialized_{false};
};
}  // namespace cosmo::media
