// Algorithm Data Type definitions

#pragma once

#include <cstddef>
#include <functional>

namespace cosmo {
enum class AlgDataType {
    Invalid,
    ChannelDataOrig,          // Original data
    ChannelDataDec,           // Decoded data
    ChannelDataDetect,        // Detected data
    TaskDataTrack,            // Tracked data
    TaskDataClassify,         // Classified data
    TaskDataGroupClassify,    // Group classified data
    TaskDataLandmark,         // Landmark data
    TaskDataRecognizer,       // Feature value data
    TaskDataOcr,              // String recognition data
    TaskDataPersonFace,       // Person face data
    TaskDataAiFilter,         // Filtering
    TaskDataAiVideoQuality,   // Video quality
    TaskDataFaceLogic,        // Face snapshot data
    TaskDataFriendDistance,   // Monkey car teammate data
    TaskDataAssoTarget,       // Associated target data
    TaskDataCluster,          // Clustered data
    TaskDataFightClassify,    // Fight classification data
    TaskDataClassifyMultPic,  // Camera moving classification data
};

}  // namespace cosmo

// Hash specialization for AlgDataType to be used as unordered_map key
template <>
struct std::hash<cosmo::AlgDataType> {
    size_t operator()(cosmo::AlgDataType t) const noexcept {
        return std::hash<int>{}(static_cast<int>(t));
    }
};
