// ActionServiceImpl — ActionService implementation — manages video & picture algorithm orchestrations

#include "service/algorithm/impl/ActionServiceImpl.h"

#include <shared_mutex>

#include "util/JsonStructUtil.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"
#include "util/StringUtil.h"
#include "util/dto/ActionCodes.h"

namespace cosmo::service {

// =============================================================================
// Construction / Destruction
// =============================================================================

ActionServiceImpl::ActionServiceImpl() {
    LOG_INFO("{}", "ActionServiceImpl Init");
}

ActionServiceImpl::~ActionServiceImpl() {
    LOG_INFO("{}", "ActionServiceImpl Delete");
}

// =============================================================================
// Unified helpers (shared by video and picture paths)
// =============================================================================

void ActionServiceImpl::ExpandLogicValues(cosmo::LogicCalc& logic) {
    auto key_ls = cosmo::util::Split(logic.keyL.ToRefString(), ".");
    logic.keyLElements.assign(key_ls.begin(), key_ls.end());

    auto key_rs = cosmo::util::Split(logic.keyR.ToRefString(), ".");
    logic.keyRElements.assign(key_rs.begin(), key_rs.end());

    for (auto& child : logic.list) {
        ExpandLogicValues(child);
    }
}

void ActionServiceImpl::ExtractAtomAlgParams(cosmo::ActionNode& action, bool full_compat) {
    for (auto& param : action.configObject.params) {
        const std::string key = std::string(cosmo::util::Trim(param.key.ToString()));

        if (cosmo::key::FPS == key) {
            action.initFps = cosmo::util::ParseFloat(param.value.ToString());
            continue;
        }

        bool is_atom_code_key = false;
        if (full_compat) {
            // Video path: handle multiple legacy key name aliases for atomicCode
            is_atom_code_key = (key == cosmo::key::ATOM_CODE) || (key == "atomCode") ||
                               (key == "atomic_code") || (key == "atomiccode") || (key == "modelCode") ||
                               (key == "algorithm_code") ||
                               (key.size() > 10 && key.rfind(".atomicCode") == key.size() - 11);
        } else {
            // Picture path: only canonical key
            is_atom_code_key = (key == cosmo::key::ATOM_CODE);
        }

        if (is_atom_code_key) {
            action.atomAlgName = param.value.ToString();
            action.atomicCode  = param.value.ToString();
        }
    }
}

cosmo::ActionAlgPtr ActionServiceImpl::FindByCodeVersion(
    std::shared_mutex& mtx, const std::map<std::string, cosmo::ActionAlgPtr>& algs, const std::string& code,
    const std::string& version) {
    if (code.empty()) {
        return nullptr;
    }
    std::shared_lock<std::shared_mutex> lock(mtx);
    auto it = algs.find(code + version);
    return (it != algs.end()) ? it->second : nullptr;
}

cosmo::ActionAlgPtr ActionServiceImpl::FindByCode(std::shared_mutex& mtx,
                                                  const std::map<std::string, cosmo::ActionAlgPtr>& algs,
                                                  const std::string& code) {
    if (code.empty()) {
        return nullptr;
    }
    std::shared_lock<std::shared_mutex> lock(mtx);
    for (const auto& [map_key, alg] : algs) {
        if (alg && alg->algorithmCode == code) {
            return alg;
        }
    }
    return nullptr;
}

bool ActionServiceImpl::DoUpdateActionAlg(cosmo::ActionAlg& action_alg_in, std::shared_mutex& mtx,
                                          std::map<std::string, cosmo::ActionAlgPtr>& algs,
                                          const ActionPredicate& needs_atom_params,
                                          const LogicPredicate& needs_logic_expand,
                                          bool full_compat_atom_keys, const char* log_tag) {
    constexpr float kMaxFpsSentinel = 999999.0f;
    float min_fps                   = kMaxFpsSentinel;
    bool have_min_fps               = false;

    cosmo::ActionAlgPtr alg_new = std::make_shared<cosmo::ActionAlg>(action_alg_in);

    // Sort workflow by dependency chain
    std::sort(alg_new->workFlow.begin(), alg_new->workFlow.end(),
              [](const auto& a, const auto& b) { return a.flowActionId == b.preFlowActionId; });

    for (auto& action : alg_new->workFlow) {
        if (needs_logic_expand(action.actionId)) {
            ExpandLogicValues(action.configObject.condition);
        } else if (needs_atom_params(action.actionId)) {
            ExtractAtomAlgParams(action, full_compat_atom_keys);
        }

        if ((action.initFps > 0) && (action.initFps < min_fps)) {
            min_fps      = action.initFps;
            have_min_fps = true;
        }
    }

    if (have_min_fps) {
        alg_new->algorithmMinFps = min_fps;
    }

    // Evict stale versions and insert/replace
    const auto map_key = alg_new->algorithmCode + alg_new->algorithmUpdateTime;

    std::lock_guard<std::shared_mutex> lock(mtx);
    for (auto it = algs.begin(); it != algs.end();) {
        if (it->second && it->second->algorithmCode == alg_new->algorithmCode) {
            if (it->second->algorithmUpdateTime != alg_new->algorithmUpdateTime) {
                LOG_INFO("{} Delete Old Alg:{} version {}", log_tag, alg_new->algorithmCode,
                         it->second->algorithmUpdateTime);
                it = algs.erase(it);
                continue;
            }
        }
        ++it;
    }

    auto existing = algs[map_key];
    algs[map_key] = alg_new;

    if (!existing) {
        LOG_INFO("{} New Alg:{} version {}", log_tag, alg_new->algorithmCode, alg_new->algorithmUpdateTime);
    } else {
        LOG_INFO("{} Replace Alg:{} version {}", log_tag, alg_new->algorithmCode,
                 alg_new->algorithmUpdateTime);
    }

    return true;
}

bool ActionServiceImpl::DoUpdateActionAlgFromJson(std::string& action_alg_json,
                                                  std::function<bool(cosmo::ActionAlg&)> updater) {
    if (action_alg_json.empty()) {
        LOG_WARN("{}", "Input cosmo::util::String Is Empty");
        return false;
    }

    cosmo::ActionAlg action_alg;
    if (!cosmo::util::DecodeJson(action_alg_json, action_alg)) {
        LOG_WARN("{}", "Json Dec For Action Failed");
        return false;
    }

    return updater(action_alg);
}

// =============================================================================
// Video algorithm orchestration
// =============================================================================

cosmo::ActionAlgPtr ActionServiceImpl::GetActionAlg(const std::string& code, const std::string& version) {
    return FindByCodeVersion(video_mtx_, video_algs_, code, version);
}

cosmo::ActionAlgPtr ActionServiceImpl::GetActionAlgByCode(const std::string& code) {
    return FindByCode(video_mtx_, video_algs_, code);
}

bool ActionServiceImpl::UpdateActionAlg(cosmo::ActionAlg& actionAlg) {
    // Video action codes that need logic expansion
    auto needs_logic = [](const std::string& id) {
        return (id == cosmo::BALogicalJudgment_Code) || (id == cosmo::BAActionBranch_Code);
    };

    // Video action codes that need atomic parameter extraction
    auto needs_atom = [](const std::string& id) {
        return (id == cosmo::AADetect_Code) || (id == cosmo::AATrack_Code) ||
               (id == cosmo::AAClassify_Code) || (id == cosmo::AAClassifyGroup_Code) ||
               (id == cosmo::AAClassifyArea_Code) || (id == cosmo::AAClassifyAttr_Code) ||
               (id == cosmo::AAClassifyMultPic_Code) || (id == cosmo::AALandmark_Code) ||
               (id == cosmo::AARecognizer_Code) || (id == cosmo::AAPersonFace_Code) ||
               (id == cosmo::AAFilter_Code) || (id == cosmo::BAFilterLogic_Code) ||
               (id == cosmo::BASensitivity_Code) || (id == cosmo::BAFixCountSensitivity_Code) ||
               (id == cosmo::AACluster_Code) || (id == cosmo::AAFightClassify_Code) ||
               (id == cosmo::BATaskCollect_Code) || (id == cosmo::AAVideoDiagnosis_Code) ||
               (id == cosmo::AAOcr_Code) || (id == cosmo::AAIrCheck_Code) ||
               (id == cosmo::DADinoDetect_Code) || (id == cosmo::DASam2Segment_Code) ||
               (id == cosmo::DAQwen3VL_Code);
    };

    return DoUpdateActionAlg(actionAlg, video_mtx_, video_algs_, needs_atom, needs_logic,
                             /*full_compat_atom_keys=*/true, "[Video]");
}

bool ActionServiceImpl::UpdateActionAlg(std::string& actionAlgJson) {
    return DoUpdateActionAlgFromJson(actionAlgJson,
                                     [this](cosmo::ActionAlg& alg) { return UpdateActionAlg(alg); });
}

// =============================================================================
// Picture algorithm orchestration
// =============================================================================

cosmo::ActionAlgPtr ActionServiceImpl::GetPicActionAlg(const std::string& code, const std::string& version) {
    return FindByCodeVersion(pic_mtx_, pic_algs_, code, version);
}

cosmo::ActionAlgPtr ActionServiceImpl::GetPicActionAlgByCode(const std::string& code) {
    return FindByCode(pic_mtx_, pic_algs_, code);
}

bool ActionServiceImpl::UpdatePicActionAlg(cosmo::ActionAlg& actionAlg) {
    // Picture action codes that need logic expansion
    auto needs_logic = [](const std::string& id) { return id == cosmo::PALogicalJudgment_Code; };

    // Picture action codes that need atomic parameter extraction
    auto needs_atom = [](const std::string& id) {
        return (id == cosmo::PADetect_Code) || (id == cosmo::PAClassify_Code) ||
               (id == cosmo::PALandmark_Code) || (id == cosmo::PARecognizer_Code) ||
               (id == cosmo::PDADino_Code) || (id == cosmo::PDASam_Code) || (id == cosmo::PDAQwen3VL_Code);
    };

    return DoUpdateActionAlg(actionAlg, pic_mtx_, pic_algs_, needs_atom, needs_logic,
                             /*full_compat_atom_keys=*/false, "[Picture]");
}

bool ActionServiceImpl::UpdatePicActionAlg(std::string& actionAlgJson) {
    return DoUpdateActionAlgFromJson(actionAlgJson,
                                     [this](cosmo::ActionAlg& alg) { return UpdatePicActionAlg(alg); });
}

}  // namespace cosmo::service
