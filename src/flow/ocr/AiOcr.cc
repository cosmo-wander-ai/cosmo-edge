// AiOcr — Landmark-driven license plate OCR action.

#include "flow/ocr/AiOcr.h"

#include <algorithm>

#include "flow/common/AlgDataUnit.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"
#include "util/Log.h"

namespace cosmo {
namespace {

    constexpr const char* kTag = "AI-OCR ";

    int LegacyAlignedSize(int value, int padding, int alignment) {
        return (value + padding) - value % alignment + padding;
    }

}  // namespace

AiOcr::AiOcr(const std::string& a_task_id, ActionNode& action)
    : AlgActionBase(AlgActionType::AlgActionAiOcr, action, "", a_task_id),
      alg_code_(action.atomicCode.empty() ? action.atomAlgName : action.atomicCode) {
    action_status = util::ErrorEnum::ActionReady;
    LOG_INFO("{}[{} {}] Init", kTag, a_task_id, GetFlowActionId());
}

AiOcr::~AiOcr() {
    Stop();
    classifier_.reset();
    LOG_INFO("{}[{} {}] Delete", kTag, task_id, GetFlowActionId());
}

bool AiOcr::AiSdkInit() {
    if (classifier_)
        return true;

    std::string cfg_path;
    std::string model_path;
    std::string word_dict_path;
    if (!service::ServiceRegistry::Instance().Get<service::IModelPathMapping>().GetModelCfg(
            alg_code_, cfg_path, model_path, word_dict_path)) {
        LOG_WARN("{}Get model configuration failed. AlgCode:{}", kTag, alg_code_);
        return false;
    }

    auto classifier =
        std::make_shared<AiOcrWordClassifierUnify>(alg_code_, cfg_path, model_path, word_dict_path);
    const auto ret = classifier->Init();
    if (ret != util::ErrorEnum::Success) {
        action_status = ret;
        LOG_WARN("{}Init OCR classifier failed. AlgCode:{} Ret:{}", kTag, alg_code_, ret);
        return false;
    }

    classifier_   = std::move(classifier);
    action_status = util::ErrorEnum::AI_INST_CREATED;
    return true;
}

bool AiOcr::HandleTarget(const VideoFramePtr& frame, AiDetectRstEl& target, DataDetTrackClassify& input,
                         DataAlarm& alarms) {
    if (target.bFilter)
        return false;

    const auto& landmark = target.relatedEl.bActive ? target.relatedEl.landmark : target.landmark;
    if (landmark.landmark.size() != 4)
        return false;

    const auto& points = landmark.landmark;
    if (points[0].x > points[3].x || points[1].x > points[2].x || points[0].y > points[1].y ||
        points[3].y > points[2].y) {
        return false;
    }
    const int span_width  = std::max(points[2].x, points[3].x) - std::min(points[0].x, points[1].x);
    const int span_height = std::max(points[1].y, points[2].y) - std::min(points[0].y, points[3].y);
    if (span_width <= 0 || span_height <= 0)
        return false;
    const int width  = LegacyAlignedSize(span_width, 16, 16);
    const int height = LegacyAlignedSize(span_height, 8, 8);
    if (width <= 0 || height <= 0 || width > static_cast<int>(frame->GetWidth()) ||
        height > static_cast<int>(frame->GetHeight())) {
        return false;
    }

    auto plate = std::make_shared<media::VideoFrame>(width, height, frame->GetPixelFormat(),
                                                     frame->GetFrameIndex(), frame->GetTimestamp());
    if (!plate->Active())
        return false;
    plate->SetStreamIndex(frame->GetStreamIndex());

    AiLandmarkData warp_landmark = landmark;
    if (classifier_->WarpAffine(frame, warp_landmark, plate) != util::ErrorEnum::Success)
        return false;

    std::string text;
    if (classifier_->Classify(plate, util::Box(0, 0, width, height), text) != util::ErrorEnum::Success ||
        text.empty()) {
        return false;
    }

    target.ocrRst.push_back({alg_code_, text, 0.0F});
    for (const auto& area : target.areaSign.areas) {
        DataAlarmUnit unit;
        unit.flowActionId = GetFlowActionId();
        unit.areaId       = area.area_id;
        unit.areaName     = area.area_name;
        unit.trackId      = target.trackId;
        unit.strTrackId   = target.trackIdInfo;
        unit.box          = target.box;
        unit.boxs.push_back(unit.box);
        unit.haveRelated = target.relatedEl.bActive;
        unit.relatedBox  = target.relatedEl.box;
        unit.confidence  = unit.haveRelated ? target.relatedEl.classifyRst : target.classifyRst;
        unit.attrRsts    = target.attrRst;
        unit.areaInfo    = input.areaInfo;
        unit.reportType  = OnEventsReportType::Realtime;
        unit.ocrString   = text;
        unit.ocrImage    = plate;
        alarms.alarms.push_back(std::move(unit));
    }
    return true;
}

void AiOcr::HandFrame(AlgDataPtr alg_data) {
    if (!alg_data || !alg_data->chanDataDec.frame || !alg_data->chanDataDec.frame->Active()) {
        action_status = util::ErrorEnum::FrameDataInvalid;
        return;
    }
    if (alg_data->dataType != AlgDataType::TaskDataLandmark) {
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }
    if (!classifier_) {
        action_status = util::ErrorEnum::AI_INST_NOTCREATED;
        if (!AiSdkInit())
            return;
    }

    auto input = alg_data->GetTaskResult(AlgDataType::TaskDataLandmark);
    if (!input)
        return;

    auto output = AlgDataCopy(alg_data);
    auto result = output->GetTaskResult(AlgDataType::TaskDataLandmark);
    if (!result)
        return;

    DataAlarm alarms;
    for (auto& target : result->targets) {
        static_cast<void>(HandleTarget(output->chanDataDec.frame, target, *result, alarms));
    }
    if (alarms.alarms.empty())
        return;

    alarms.multiAlarms              = 1;
    output->taskDataAlarm.alarmData = std::make_shared<DataAlarm>(std::move(alarms));
    output->dataType                = AlgDataType::TaskDataOcr;
    action_status                   = util::ErrorEnum::Success;
    distributor->DistributorData(output);
}

}  // namespace cosmo
