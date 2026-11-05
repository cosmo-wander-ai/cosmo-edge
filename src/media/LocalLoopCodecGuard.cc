#include "media/LocalLoopCodecGuard.h"

#include <algorithm>

namespace cosmo::media {

bool LocalLoopCodecGuard::UnchangedExtradata(const std::vector<uint8_t>* expected, const uint8_t* data,
                                             size_t size) {
    return expected && data && size > 0 && size <= 4 * 1024 * 1024 && expected->size() == size &&
           std::equal(expected->begin(), expected->end(), data);
}

void LocalLoopCodecGuard::Reset() {
    codec_ = VideoCodecType::kInvalid;
    for (auto& parameter : parameters_)
        parameter.clear();
    eligible_    = true;
    initialized_ = false;
}

bool LocalLoopCodecGuard::Observe(VideoCodecType codec, const uint8_t* data, size_t size, bool boundary) {
    if (!eligible_ || !data || size < 5 ||
        (codec != VideoCodecType::kH264 && codec != VideoCodecType::kH265) ||
        (initialized_ && codec != codec_)) {
        eligible_ = false;
        return false;
    }
    auto next = parameters_;
    std::array<bool, 3> seen{};
    bool idr            = false;
    bool first_vcl_seen = false;
    auto find_start     = [&](size_t from, size_t& start, size_t& payload) {
        for (size_t i = from; i + 3 <= size; ++i) {
            if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
                start   = i;
                payload = i + 3;
                return true;
            }
            if (i + 4 <= size && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 && data[i + 3] == 1) {
                start   = i;
                payload = i + 4;
                return true;
            }
        }
        return false;
    };
    size_t start = 0, payload = 0;
    if (!find_start(0, start, payload) || start != 0) {
        eligible_ = false;
        return false;
    }
    while (payload < size) {
        size_t next_start = 0, next_payload = 0;
        const bool more = find_start(payload, next_start, next_payload);
        if (more && next_payload >= size) {
            eligible_ = false;
            return false;
        }
        size_t end = more ? next_start : size;
        while (end > payload && data[end - 1] == 0)
            --end;
        if (end <= payload || (data[payload] & 0x80) ||
            (codec == VideoCodecType::kH265 && (end - payload < 2 || !(data[payload + 1] & 7)))) {
            eligible_ = false;
            return false;
        }
        const int type = codec == VideoCodecType::kH264 ? data[payload] & 31 : (data[payload] >> 1) & 63;
        if (codec == VideoCodecType::kH264 && type == 0) {
            eligible_ = false;
            return false;
        }
        const int slot = codec == VideoCodecType::kH264 ? (type == 7   ? 1
                                                           : type == 8 ? 2
                                                                       : -1)
                                                        : (type >= 32 && type <= 34 ? type - 32 : -1);
        const bool vcl = codec == VideoCodecType::kH264
                             ? (type >= 1 && type <= 5) || (type >= 19 && type <= 21)
                             : type <= 31;
        if (vcl && !first_vcl_seen) {
            first_vcl_seen = true;
            idr            = codec == VideoCodecType::kH264 ? type == 5 : type == 19 || type == 20;
            if ((!initialized_ || boundary) &&
                (!idr || !seen[1] || !seen[2] || (codec == VideoCodecType::kH265 && !seen[0]))) {
                eligible_ = false;
                return false;
            }
        } else if (vcl && (!initialized_ || boundary) &&
                   !(codec == VideoCodecType::kH264 ? type == 5 : type == 19 || type == 20)) {
            eligible_ = false;
            return false;
        }
        if (slot >= 0) {
            const size_t minimum = slot == 2 ? (codec == VideoCodecType::kH265 ? 2 : 1) : 4;
            if (end - payload < minimum || end - payload > 1024 * 1024) {
                eligible_ = false;
                return false;
            }
            std::vector<uint8_t> value(data + payload, data + end);
            if (!next[slot].empty() && next[slot] != value) {
                eligible_ = false;
                return false;
            }
            next[slot] = std::move(value);
            seen[slot] = true;
        }
        if (!more)
            break;
        payload = next_payload;
    }
    if ((!initialized_ || boundary) && (!first_vcl_seen || !idr)) {
        eligible_ = false;
        return false;
    }
    parameters_  = std::move(next);
    codec_       = codec;
    initialized_ = true;
    return true;
}
}  // namespace cosmo::media
