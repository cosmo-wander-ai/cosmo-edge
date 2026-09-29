// Target filtering action for tagging detection targets based on size,
// confidence and motion rules.

#pragma once

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"

namespace cosmo {
enum class BAFilterType {
    kNone = 0,
    kSizeMin,        // Minimum size limit
    kSizeMax,        // Maximum size limit
    kSideMin,        // Minimum side length for area calculation
    kSideMax,        // Maximum side length for area calculation
    kConfidenceMin,  // Minimum confidence filter
    kConfidenceMax,  // Maximum confidence filter
    kMotionMove,     // Motion state: moving filter
    kMotionStatic,   // Motion state: static filter
    kCount
};

struct BAFilterParam {
    BAFilterType type{BAFilterType::kNone};
    std::string alg_code;
    std::string label;
    float f_value{0.0f};
    int i_value{0};
};

struct BACategoryFilterParam {
    std::string alg_code;
    std::string label;
};

enum class TargetFilterMode {
    kCategory,
    kSize,
};

class TargetFilter : public AlgActionBase {
public:
    TargetFilter(const std::string& taskId, ActionNode& action);
    ~TargetFilter();

    // Modify parameters based on existing ones
    bool ModifyParam(const std::string& channelId, const std::string& taskId,
                     std::vector<MsgDynamicKeyValue>& params) override;
    // Set parameters - clear previous ones and set fully new ones
    bool SetParam(const std::string& channelId, const std::string& taskId,
                  std::vector<MsgDynamicKeyValue>& params) override;

private:
    friend class TargetFilterTestPeer;

    void HandFrame(AlgDataPtr algData) override;
    bool AnalysisKey(const MsgDynamicKeyValue& param, BAFilterParam& filter_el) const;
    bool AnalysisCategoryKey(const MsgDynamicKeyValue& param, BACategoryFilterParam& category_el) const;
    /** Convert BAFilterParam to MsgDynamicKeyValue (for writing back to workFlow to support layout saving) */
    static MsgDynamicKeyValue FilterParamToKeyValue(const BAFilterParam& p);
    MsgDynamicKeyValue SizeFilterParamToKeyValue(const BAFilterParam& p) const;
    MsgDynamicKeyValue CategoryParamToKeyValue(const BACategoryFilterParam& p) const;
    /** Sync current filter_params_ back to action_alg->workFlow node, enabling category filter saving with
     * layout */
    void SyncFilterParamsToWorkFlow();
    void DoFilter(DataDetTrackClassifyPtr input);
    void DoCategoryFilter(DataDetTrackClassifyPtr input) const;
    void DoSizeFilter(DataDetTrackClassifyPtr input) const;
    void DoLegacyCombinedFilter(DataDetTrackClassifyPtr input) const;
    void LoadConfiguredParams(const std::vector<MsgDynamicKeyValue>& params, bool replace);
    void UpsertFilterParam(const BAFilterParam& filter_el);
    void UpsertCategoryParam(const BACategoryFilterParam& category_el);
    bool MatchesTarget(const std::string& alg_code, const std::string& label,
                       const AiDetectRstEl& target) const;
    template <typename TYPE>
    bool MinValueFilter(const TYPE& lValue, const TYPE& rValue) const {
        return lValue < rValue;
    }
    template <typename TYPE>
    bool MaxValueFilter(const TYPE& lValue, const TYPE& rValue) const {
        return lValue > rValue;
    }
    bool FilterTarget(const BAFilterParam& filter_param, const AiDetectRstEl& target) const;
    std::string FilterDesc(BAFilterType type) const;

    TargetFilterMode mode_{TargetFilterMode::kCategory};
    std::vector<BACategoryFilterParam> category_params_;
    std::vector<BAFilterParam> filter_params_;
};
using TargetFilterPtr = std::shared_ptr<TargetFilter>;
}  // namespace cosmo
