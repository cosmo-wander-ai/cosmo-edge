// BodyLibServiceImpl — cached body/things library comparison.
// Extracted from AiRecognizer::HandFace() static state (DEBT-C06).
#pragma once

#include <atomic>
#include <map>
#include <shared_mutex>
#include <string>
#include <vector>

#include "service/face/IBodyLibService.h"
#include "service/face/dto/PersonMsgTypes.h"

namespace cosmo::service {

class BodyLibServiceImpl : public IBodyLibService {
public:
    BodyLibServiceImpl()                                     = default;
    ~BodyLibServiceImpl() override                           = default;
    BodyLibServiceImpl(const BodyLibServiceImpl&)            = delete;
    BodyLibServiceImpl& operator=(const BodyLibServiceImpl&) = delete;

    bool BodyCompare(const std::vector<std::string>& lib_ids, const AiFeature& runtime_feature,
                     AiDetectMatchHighScoreInfo& match_info, float limit_score,
                     CompareFeatureFunc compare_fn) override;

    void InvalidateCache(const std::string& lib_id) override;
    void InvalidateAll() override;
    void SetCacheTtlMs(int64_t ttl_ms) override;
    std::vector<float> ExtractBodyFeature(const VideoFramePtr& image) override;

private:
    struct CacheItem {
        int64_t last_update_time{0};
        std::vector<MsgQueryPersonPicturesS::Person> persons;
        float lib_threshold{60.0f};
    };

    /// Fetch person list and threshold for a library, using cache when valid.
    void FetchLibData(const std::string& lib_id, std::vector<MsgQueryPersonPicturesS::Person>& persons,
                      float& lib_threshold);

    mutable std::shared_mutex mtx_;
    std::map<std::string, CacheItem> cache_;
    std::atomic<int64_t> cache_ttl_ms_{5000};
};

}  // namespace cosmo::service
