// Body/Things library comparison service interface.
// Manages cached person records and performs feature matching against body libraries.
// Consumed by AiRecognizer (flow/recognizer) for work-clothes and body recognition.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "service/media/dto/VideoFrameFwd.h"
#include "util/AiTypes.h"

namespace cosmo::service {

/// Callback type for feature comparison — delegates to the caller's model instance.
using CompareFeatureFunc = std::function<float(const AiFeature&, const AiFeature&)>;

class IBodyLibService {
public:
    virtual ~IBodyLibService() = default;

    /// Compare a runtime feature against all persons in the specified body libraries.
    /// @param lib_ids        List of body library IDs to search.
    /// @param runtime_feature Feature extracted from the current frame.
    /// @param match_info     [out] Best match result (populated even if below threshold).
    /// @param limit_score    Caller-configured score threshold (-1 means use library default).
    /// @param compare_fn     Feature comparison callback bound to the caller's model instance.
    /// @return true if a match exceeding the threshold was found.
    virtual bool BodyCompare(const std::vector<std::string>& lib_ids, const AiFeature& runtime_feature,
                             AiDetectMatchHighScoreInfo& match_info, float limit_score,
                             CompareFeatureFunc compare_fn) = 0;

    /// Invalidate cache for a specific library (call on lib add/update/delete).
    virtual void InvalidateCache(const std::string& lib_id) = 0;

    /// Invalidate all cached entries.
    virtual void InvalidateAll() = 0;

    /// Set the cache TTL in milliseconds (default: 5000ms).
    virtual void SetCacheTtlMs(int64_t ttl_ms) = 0;

    /// Extract body (pedestrian) feature from a decoded image.
    /// Runs pedestrian detection → crop → feature extraction pipeline.
    /// @param image Decoded video frame to process.
    /// @return Feature vector, or empty vector on failure.
    virtual std::vector<float> ExtractBodyFeature(const VideoFramePtr& image) = 0;
};

}  // namespace cosmo::service
