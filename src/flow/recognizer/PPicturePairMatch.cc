#include "flow/recognizer/PPicturePairMatch.h"

#include <cmath>
#include <cstdlib>

#include "service/ai/IInferPoolService.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"

namespace cosmo {
bool PPicturePairMatch::ActionInit() {
    if (comparator_)
        return true;
    std::string config, model;
    if (!service::ServiceRegistry::Instance().Get<service::IModelPathMapping>().GetModelCfg(GetAtomicCode(),
                                                                                            config, model))
        return false;
    auto pool = service::ServiceRegistry::Instance().Get<service::IInferPoolService>().GetRecognizerPool(
        GetAtomicCode());
    if (!pool)
        return false;
    comparator_ = std::make_shared<AiRecognizerInterface>(pool, GetAtomicCode(), config, model);
    return true;
}

bool PPicturePairMatch::SetParam(const std::string&, std::vector<MsgDynamicKeyValue>& params) {
    face_ = true;
    threshold_.reset();
    for (const auto& param : params) {
        if (param.key == "pair.featureType") {
            if (param.value != "face" && param.value != "body")
                return false;
            face_ = param.value == "face";
        }
        if (param.key == "pair.threshold" && !param.value.empty()) {
            const auto value    = param.value.ToString();
            char* end           = nullptr;
            const double number = std::strtod(value.c_str(), &end);
            if (*end || !std::isfinite(number) || number < 0 || number > 100)
                return false;
            threshold_ = number;
        }
    }
    return true;
}

util::ErrorEnum PPicturePairMatch::Compare(const AiFeature& left, const AiFeature& right, double& score) {
    if (!comparator_)
        return util::ErrorEnum::NotInit;
    return comparator_->ComparePairFeatures(left, right, face_, score);
}
}  // namespace cosmo
