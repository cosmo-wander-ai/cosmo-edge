// BodyLibServiceImpl — cached body/things library comparison.
// Core logic migrated from AiRecognizer::HandFace() (DEBT-C06).

#include "service/face/impl/BodyLibServiceImpl.h"

#include <algorithm>

#include "infer/AiDetectInterface.h"
#include "infer/AiRecognizerInterface.h"
#include "service/ai/IInferPoolService.h"
#include "service/detail/ServiceRegistry.h"
#include "service/face/IPersonRecogDaoService.h"
#include "service/model/IModelService.h"
#include "util/Log.h"
#include "util/TimeUtil.h"

static constexpr const char* kTag = "BODY-LIB-SVC ";

namespace cosmo::service {

std::vector<float> BodyLibServiceImpl::ExtractBodyFeature(const VideoFramePtr& imageData) {
    if (!imageData) {
        return {};
    }

    auto& modelSvc = ServiceRegistry::Instance().Get<IModelService>();

    // Step 1: Detect pedestrian bounding box using model 1001003
    util::Box personBox{0, 0, static_cast<int>(imageData->GetWidth()),
                        static_cast<int>(imageData->GetHeight())};
    std::string detCfg, detModel;
    if (modelSvc.GetModelCfg("1001003", detCfg, detModel)) {
        auto pool = ServiceRegistry::Instance().Get<IInferPoolService>().GetDetectPool("1001003");
        infer::AiDetectInterface detector(pool, "1001003", detCfg, detModel);
        std::vector<AiConfidence> confThres;
        AiConfidence conf;
        conf.label      = "pedestrian";
        conf.confidence = 0.3f;
        confThres.push_back(conf);
        std::vector<AiDetectRstEl> detResults;
        if (detector.Detect(imageData, confThres, detResults) == util::ErrorEnum::Success &&
            !detResults.empty()) {
            int bestArea = 0;
            for (auto& det : detResults) {
                int area = det.box.width * det.box.height;
                if (area > bestArea) {
                    bestArea  = area;
                    personBox = det.box;
                }
            }
        } else {
            LOG_WARN("{}", "ExtractBodyFeature: no pedestrian detected, using full image as fallback");
        }
    } else {
        LOG_WARN("{}", "ExtractBodyFeature: missing model cfg for 1001003, using full image");
    }

    // Step 2: Extract feature using pedestrian recognizer model 1001007
    std::string cfgPath, modelPath;
    if (!modelSvc.GetModelCfg("1001007", cfgPath, modelPath)) {
        LOG_WARN("{}", "Missing model cfg for 1001007");
        return {};
    }
    auto pool = ServiceRegistry::Instance().Get<IInferPoolService>().GetRecognizerPool("1001007");
    AiRecognizerInterface recognizer(pool, "1001007", cfgPath, modelPath);
    std::vector<AiDetectRstEl> targets;
    AiDetectRstEl target;
    target.box = personBox;
    targets.push_back(target);
    if (recognizer.Recognize(imageData, targets, true) != util::ErrorEnum::Success) {
        LOG_WARN("{}", "Extract body feature failed");
        return {};
    }
    if (targets.empty() || targets[0].feature.feature.empty()) {
        LOG_WARN("{}", "Extract body feature returned empty");
        return {};
    }
    LOG_INFO("Extracted body feature successfully, size:{}", targets[0].feature.feature.size());
    return targets[0].feature.feature;
}

bool BodyLibServiceImpl::BodyCompare(const std::vector<std::string>& lib_ids,
                                     const AiFeature& runtime_feature, AiDetectMatchHighScoreInfo& match_info,
                                     float limit_score, CompareFeatureFunc compare_fn) {
    float best_score         = -1.0f;
    float best_lib_threshold = 60.0f;

    for (const auto& lib_id : lib_ids) {
        std::vector<MsgQueryPersonPicturesS::Person> person_list;
        float cur_lib_threshold = 60.0f;
        FetchLibData(lib_id, person_list, cur_lib_threshold);

        for (const auto& person : person_list) {
            if (person.feature.empty()) {
                continue;
            }
            AiFeature lib_feature;
            lib_feature.feature = person.feature;

            float score = compare_fn(lib_feature, runtime_feature);
            if (score > best_score) {
                best_score              = score;
                match_info.match_degree = score;
                match_info.group_id     = lib_id;
                match_info.person_id    = person.id;
                match_info.name         = person.pictureName;
                best_lib_threshold      = cur_lib_threshold;
            }
        }
    }

    if (best_score > 0) {
        float effective_threshold = limit_score > 0 ? limit_score : best_lib_threshold;
        match_info.matched        = (best_score > effective_threshold);
        LOG_INFO("{}BodyCompare bestScore:{} threshold:{} (libThreshold:{}) matched:{}", kTag, best_score,
                 effective_threshold, best_lib_threshold, match_info.matched);
        return match_info.matched;
    }

    return false;
}

void BodyLibServiceImpl::FetchLibData(const std::string& lib_id,
                                      std::vector<MsgQueryPersonPicturesS::Person>& persons,
                                      float& lib_threshold) {
    bool need_update = false;

    // Read phase — shared lock
    {
        std::shared_lock lock(mtx_);
        auto it = cache_.find(lib_id);
        if (it == cache_.end() || util::GetMilliseconds() - it->second.last_update_time > cache_ttl_ms_) {
            need_update = true;
        } else {
            persons       = it->second.persons;
            lib_threshold = it->second.lib_threshold;
            return;
        }
    }

    if (!need_update) {
        return;
    }

    // Cache miss — query DB using db:: local types, then convert to Msg types
    auto& dao_svc = ServiceRegistry::Instance().Get<IPersonRecogDaoService>();

    db::PersonRecogQueryCondition cond{};
    cond.person_lib_id_list.push_back(lib_id);
    cond.page_num  = 1;
    cond.page_size = 10000;
    auto result    = dao_svc.QueryPersons(cond);

    // Convert db:: result to Msg types for cache storage
    persons.clear();
    for (auto& r : result.person_list) {
        MsgQueryPersonPicturesS::Person p{};
        p.id              = r.id;
        p.pictureName     = r.picture_name;
        p.pictureUrl      = r.picture_url;
        p.createTimestamp = r.create_timestamp;
        p.updateTimestamp = r.update_timestamp;
        p.feature         = std::move(r.feature);
        p.personLib.id    = r.person_lib.id;
        p.personLib.name  = r.person_lib.name;
        persons.push_back(std::move(p));
    }

    // Query library threshold configuration
    float queried_threshold = 60.0f;
    db::PersonRecogLibQueryCondition lib_cond{};
    lib_cond.page_size = 1000;
    auto lib_result    = dao_svc.QueryPersonLib(lib_cond);
    auto it            = std::find_if(lib_result.person_lib_list.begin(), lib_result.person_lib_list.end(),
                                      [&lib_id](const auto& lib) { return lib.id == lib_id; });
    if (it != lib_result.person_lib_list.end()) {
        queried_threshold = static_cast<float>(it->threshold);
    }
    lib_threshold = queried_threshold;

    // Write phase — exclusive lock
    {
        std::unique_lock lock(mtx_);
        auto& item            = cache_[lib_id];
        item.last_update_time = util::GetMilliseconds();
        item.persons          = persons;
        item.lib_threshold    = queried_threshold;
    }
}

void BodyLibServiceImpl::InvalidateCache(const std::string& lib_id) {
    std::unique_lock lock(mtx_);
    cache_.erase(lib_id);
    LOG_INFO("{}InvalidateCache lib_id:{}", kTag, lib_id);
}

void BodyLibServiceImpl::InvalidateAll() {
    std::unique_lock lock(mtx_);
    cache_.clear();
    LOG_INFO("{}InvalidateAll", kTag);
}

void BodyLibServiceImpl::SetCacheTtlMs(int64_t ttl_ms) {
    cache_ttl_ms_ = ttl_ms;
    LOG_INFO("{}SetCacheTtlMs:{}", kTag, ttl_ms);
}

}  // namespace cosmo::service
