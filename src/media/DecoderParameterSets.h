#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "media/VideoCodecType.h"

namespace cosmo::media {

// Supplies out-of-band AVC/HEVC parameter sets to a packet-only decoder.
// Input packets must already be Annex B access units (the demux contract).
// Prepare does not consume the pending headers: only a successful decoder
// submission may call Commit. Reset also covers a retained decoder on reconnect.
class DecoderParameterSets {
public:
    bool Configure(VideoCodecType codec, const std::vector<uint8_t>& extradata, std::string& error);
    void Reset() noexcept;
    bool Prepare(const uint8_t* data, size_t size, const uint8_t*& output, size_t& output_size,
                 std::string& error);
    void Commit() noexcept;
    size_t HeaderBytes() const noexcept {
        return headers_.size();
    }

private:
    VideoCodecType codec_{VideoCodecType::kInvalid};
    std::vector<uint8_t> headers_;
    std::vector<uint8_t> packet_;
    bool valid_{true};
    bool pending_{true};
    bool prepared_{false};
};

}  // namespace cosmo::media
