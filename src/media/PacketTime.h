#pragma once

#include <cstdint>
#include <string>

namespace cosmo::media {

struct PacketTime {
    int64_t pts{0};
    int64_t dts{0};
    int64_t duration{0};
};

// Convert known source timestamps relative to the first written DTS. This
// helper does not infer missing timestamps or reorder packets. The output is
// unchanged on failure; callers commit timeline state only after writing.
bool RescalePacketTime(int64_t pts, int64_t dts, int64_t duration, int tb_num, int tb_den, int64_t origin_dts,
                       int target_timescale, PacketTime& out, std::string& reason);

}  // namespace cosmo::media
