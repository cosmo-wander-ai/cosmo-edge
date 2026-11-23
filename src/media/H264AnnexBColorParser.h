#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "media/VideoColorProvenance.h"

namespace cosmo::media {

// Experimental evidence parser; it never decodes image pixels.
// Rejects interlaced, non-8-bit/4:2:0 and unknown
// NAL forms. Its result is not connected to any production frame path.
class H264AnnexBColorParser {
public:
    static bool ParseParameterSetGeometry(const uint8_t* data, size_t size, uint32_t& width,
                                          uint32_t& height);
    bool BeginGeneration(uint64_t generation);
    bool ParseAccessUnit(uint64_t generation, const uint8_t* data, size_t size, VideoColorEvidence& output);
    [[nodiscard]] bool Faulted() const {
        return fault_;
    }

private:
    struct Sps {
        uint32_t id{0};
        unsigned frame_num_bits{0};
        unsigned poc_type{0};
        unsigned poc_bits{0};
        bool delta_poc_always_zero{false};
        uint32_t width{0};
        uint32_t height{0};
        uint64_t pic_size_in_mbs{0};
        NativeVideoColorSpace space{NativeVideoColorSpace::Unspecified};
        NativeVideoColorRange range{NativeVideoColorRange::Unspecified};
    };
    struct Pps {
        uint32_t id{0};
        uint32_t sps_id{0};
        bool bottom_field_poc_present{false};
    };

    struct SliceHeader {
        uint32_t first_mb{0};
        uint32_t frame_num{0};
        uint32_t idr_pic_id{0};
        uint32_t poc_lsb{0};
        int32_t delta_poc_bottom{0};
        int32_t delta_poc0{0};
        int32_t delta_poc1{0};
        bool idr{false};
        bool reference{false};
    };

    static bool ReadSps(const std::vector<uint8_t>& rbsp, Sps& value);
    static bool ReadPps(const std::vector<uint8_t>& rbsp, Pps& value);
    static bool ReadSlice(const std::vector<uint8_t>& rbsp, bool idr, bool reference, const Sps& sps,
                          const Pps& pps, SliceHeader& slice);
    static bool ReadSei(const std::vector<uint8_t>& rbsp);

    uint64_t generation_{0};
    bool fault_{false};
    bool have_sps_{false};
    bool have_pps_{false};
    Sps sps_{};
    Pps pps_{};
    std::vector<uint8_t> sps_bytes_;
    std::vector<uint8_t> pps_bytes_;
};

}  // namespace cosmo::media
