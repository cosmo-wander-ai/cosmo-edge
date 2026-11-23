#pragma once

extern "C" {
#include "libavcodec/packet.h"
}

namespace cosmo::media {

// Side data is a reason to reject an AU until the new parameter set is
// verified. Its absence does not prove that in-band SPS/PPS stayed unchanged.
inline bool HasColorRelevantParameterChange(const AVPacket& packet) {
    if (packet.side_data_elems < 0 || (packet.side_data_elems > 0 && !packet.side_data))
        return true;
    for (int i = 0; i < packet.side_data_elems; ++i)
        if (packet.side_data[i].type == AV_PKT_DATA_NEW_EXTRADATA ||
            packet.side_data[i].type == AV_PKT_DATA_PARAM_CHANGE)
            return true;
    return false;
}

}  // namespace cosmo::media
