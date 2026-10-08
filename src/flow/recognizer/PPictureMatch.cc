#include "flow/recognizer/PPictureMatch.h"

#include <cmath>

#include "service/ai/IInferPoolService.h"
#include "service/detail/ServiceRegistry.h"
#include "service/face/IBodyLibService.h"
#include "service/face/IFaceFeature.h"
#include "service/model/IModelPathMapping.h"
#include "util/SafeParse.h"
#include "util/StringUtil.h"

namespace cosmo {
bool PPictureMatch::SetParam(const std::string&, std::vector<MsgDynamicKeyValue>& params) {
    body_      = false;
    invert_    = false;
    threshold_ = -1;
    libraries_.clear();
    for (const auto& param : params)
        if (param.key == "match.libraryType")
            body_ = param.value == "body";
    for (const auto& param : params) {
        if (param.key == "match.libraryType")
            body_ = param.value == "body";
        if (param.key == "match.mode")
            invert_ = param.value == "unmatched";
        if (param.key == "param.limitScore") {
            threshold_ = util::ParseFloat(param.value, -2.0F);
            if (!std::isfinite(threshold_) || threshold_ < 0 || threshold_ > 100)
                return false;
        }
        if (param.key == (body_ ? "param.workClothesSet" : "param.faceSet")) {
            for (const auto& part : util::Split(param.value.ToRefString(), ",")) {
                const auto id = util::Trim(part);
                if (!id.empty())
                    libraries_.emplace_back(id);
            }
        }
    }
    return true;
}

bool PPictureMatch::ActionInit() {
    if (comparator_)
        return true;
    if (GetAtomicCode().empty())
        return !body_;
    std::string config, model;
    if (!service::ServiceRegistry::Instance().Get<service::IModelPathMapping>().GetModelCfg(GetAtomicCode(),
                                                                                            config, model))
        return false;
    auto pool = service::ServiceRegistry::Instance().Get<service::IInferPoolService>().GetRecognizerPool(
        GetAtomicCode());
    comparator_ = std::make_shared<AiRecognizerInterface>(pool, GetAtomicCode(), config, model);
    return true;
}

util::ErrorEnum PPictureMatch::HandPic(AlgDataPtr data) {
    if (!data || !data->chanDataDetect.detRet)
        return util::ErrorEnum::FlowDataInvalid;
    if (libraries_.empty())
        return util::ErrorEnum::InvalidParam;
    if (body_ && !comparator_)
        return util::ErrorEnum::NotInit;
    for (auto& target : data->chanDataDetect.detRet->targets) {
        if (target.bFilter)
            continue;
        if (target.feature.feature.empty())
            return util::ErrorEnum::FlowDataInvalid;
        bool matched = false;
        if (body_) {
            matched = service::ServiceRegistry::Instance().Get<service::IBodyLibService>().BodyCompare(
                libraries_, target.feature, target.matchInfo, threshold_,
                [this](const AiFeature& a, const AiFeature& b) { return comparator_->CompareFeature(a, b); });
        } else {
            matched = service::ServiceRegistry::Instance().Get<service::IFaceFeature>().FaceCompare(
                libraries_, target.feature, target.matchInfo, threshold_);
        }
        const bool known    = target.matchInfo.setPicCount > 0;
        target.bLogicResult = known && (invert_ ? !matched : matched);
        data->bHaveLogic    = true;
        const auto decision = !known ? "unknown" : (target.bLogicResult ? "matched" : "not_matched");
        data->pictureDecisions[target.targetId]                = decision;
        data->pictureRules[GetFlowActionId()][target.targetId] = decision;
    }
    return util::ErrorEnum::Success;
}
}  // namespace cosmo
