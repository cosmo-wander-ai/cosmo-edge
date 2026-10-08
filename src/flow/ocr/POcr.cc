#include "flow/ocr/POcr.h"

#include <algorithm>

#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"

namespace cosmo {
bool POcr::ActionInit() {
    if (instance_)
        return true;
    std::string config, model, dictionary;
    if (!service::ServiceRegistry::Instance().Get<service::IModelPathMapping>().GetModelCfg(
            GetAtomicCode(), config, model, dictionary))
        return false;
    instance_ = std::make_shared<AiOcrWordClassifierUnify>(GetAtomicCode(), config, model, dictionary);
    if (instance_->Init() == util::ErrorEnum::Success)
        return true;
    instance_.reset();
    return false;
}

util::ErrorEnum POcr::HandPic(AlgDataPtr data) {
    if (!instance_)
        return util::ErrorEnum::NotInit;
    if (!data || !data->chanDataDec.frame || !data->chanDataDetect.detRet)
        return util::ErrorEnum::FlowDataInvalid;
    for (auto& target : data->chanDataDetect.detRet->targets) {
        if (target.bFilter)
            continue;
        const auto& frame = data->chanDataDec.frame;
        auto image        = frame;
        auto box          = target.box;
        if (target.landmark.landmark.size() == 4) {
            // Plate models provide ordered corners. Preserve their perspective correction.
            const auto& points = target.landmark.landmark;
            const int width    = std::max(points[2].x, points[3].x) - std::min(points[0].x, points[1].x);
            const int height   = std::max(points[1].y, points[2].y) - std::min(points[0].y, points[3].y);
            if (width <= 0 || height <= 0 || width > static_cast<int>(frame->GetWidth()) ||
                height > static_cast<int>(frame->GetHeight()))
                return util::ErrorEnum::InvalidParam;
            image           = std::make_shared<media::VideoFrame>(width, height, frame->GetPixelFormat(),
                                                                  frame->GetFrameIndex(), frame->GetTimestamp());
            const auto warp = instance_->WarpAffine(frame, target.landmark, image);
            if (warp != util::ErrorEnum::Success)
                return warp;
            box = util::Box(0, 0, width, height);
        }
        std::string text;
        const auto status = instance_->Classify(image, box, text);
        if (status != util::ErrorEnum::Success)
            return status;
        target.ocrRst.push_back({GetAtomicCode(), text, 0.0F});
    }
    return util::ErrorEnum::Success;
}
}  // namespace cosmo
