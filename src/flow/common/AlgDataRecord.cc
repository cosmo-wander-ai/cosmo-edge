// AlgDataRecord — Overview recording limits

#include "flow/common/AlgDataRecord.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <random>

#include "service/detail/ServiceRegistry.h"
#include "service/system/IOverviewConfig.h"
#include "util/FileUtil.h"
#include "util/JsonStructUtil.h"
#include "util/LimitedTypeJson.h"
#include "util/Log.h"
#include "util/MsgBaseTypes.h"
#include "util/PathUtil.h"
#include "util/dto/AlgorithmMsgTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"
#include "util/dto/TaskCreateTypes.h"

namespace cosmo {
static constexpr int kAlgDataRecordMaxIndex = 100000;  // ~1 hour at 25fps

struct RecDataUrl {
    std::string url;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(RecDataUrl, url)
};

void DetData2RecData(DataDetTrackClassifyPtr frame, MsgAiDetFrame& recData) {
    recData.index       = frame->frameIndex;
    recData.streamIndex = frame->streamIndex;
    if ((frame->picWidth <= 0) || (frame->picHeight <= 0)) {
        return;
    }
    for (auto& target : frame->targets) {
        MsgTarget recTarget;
        recTarget.trackId           = target.trackId;
        recTarget.trackStatus       = GetTrackStatus(target.trackStatus);
        recTarget.motionStatus      = GetMotionStatus(target.motionStatus);
        recTarget.shapeChangeStatus = GetShapeChangeStatus(target.shapeChangeStatus);
        recTarget.bFilter           = target.bFilter;
        recTarget.filterDesc        = target.filterDesc;
        recTarget.bLogicResult      = target.bLogicResult;
        recTarget.box.x             = static_cast<double>(target.box.x) / frame->picWidth;
        recTarget.box.y             = static_cast<double>(target.box.y) / frame->picHeight;
        recTarget.box.width         = static_cast<double>(target.box.width) / frame->picWidth;
        recTarget.box.height        = static_cast<double>(target.box.height) / frame->picHeight;
        recTarget.aiBox.x           = target.box.x;
        recTarget.aiBox.y           = target.box.y;
        recTarget.aiBox.width       = target.box.width;
        recTarget.aiBox.height      = target.box.height;
        recTarget.hwRatio           = target.hwRatio;
        recTarget.hwRatioVariation  = target.hwRatioVariation;
        recTarget.bHaveMatchInfo    = false;
        MsgAiConfidence confidencedet;
        confidencedet.label      = target.confidence.label;
        confidencedet.confidence = target.confidence.confidence;
        if (!confidencedet.label.empty() && confidencedet.confidence >= 0)
            recTarget.confidence.push_back(confidencedet);
        for (auto& targetHistory : target.classifyRst) {
            MsgAiConfidence confidence;
            confidence.label      = targetHistory.label;
            confidence.confidence = targetHistory.confidence;
            if (!confidencedet.label.empty())
                recTarget.confidence.push_back(confidence);
        }

        for (auto& area : target.areaSign.areas) {
            recTarget.areas.push_back(area.area_id);
        }
        for (auto& shiledArea : target.areaSign.shielded_areas) {
            recTarget.shiledAreas.push_back(shiledArea.area_id);
        }
        recData.targets.push_back(recTarget);
    }
}

void RecordAlgDataUrl(const std::string& taskId, const std::string& url) {
    // Whether to save structured files
    if (!service::ServiceRegistry::Instance().Get<service::IOverviewConfig>().GetOverviewStructureFile()) {
        return;
    }

    RecDataUrl recData;
    recData.url = url;
    std::string recStr;
    if (!util::EncodeJson(recData, recStr, -1)) {
        return;
    }
    recStr.append("\n");

    auto path     = cosmo::path::GetTaskOverviewDataPath(taskId);
    auto fileName = path + "/url" + ".json";
    util::WriteFile(fileName, recStr);
    return;
}

void RecordAlgDataAlarm(const std::string& taskId, const MsgRecAlarm& data) {
    // Whether to save structured files
    if ((!service::ServiceRegistry::Instance().Get<service::IOverviewConfig>().GetOverviewStructureFile()) ||
        (data.index > kAlgDataRecordMaxIndex)) {
        return;
    }

    std::string recStr;
    if (!util::EncodeJson(data, recStr, -1)) {
        return;
    }
    recStr.append("\n");

    auto path     = cosmo::path::GetTaskOverviewDataPath(taskId);
    auto fileName = path + "/alarm" + ".json";
    util::WriteFileAppend(fileName, recStr);
    return;
}

void RecordAlgTaskInfo(const std::string& taskId, const MsgTaskCreateRecv& data) {
    std::string recStr;
    if (!util::EncodeJson(data, recStr)) {
        return;
    }

    auto path     = cosmo::path::GetTaskOverviewDataPath(taskId);
    auto fileName = path + "/taskInfo" + ".json";
    util::WriteFile(fileName, recStr);
    return;
}

void RecordAlgTaskAction(const std::string& taskId, ActionAlgPtr data) {
    if (!data) {
        return;
    }

    std::string recStr;
    if (!util::EncodeJson(data, recStr)) {
        return;
    }

    auto path     = cosmo::path::GetTaskOverviewDataPath(taskId);
    auto fileName = path + "/taskAction" + ".json";
    util::WriteFile(fileName, recStr);
    return;
}

void RecordAlgDataClearTaskData(const std::string& taskId) {
    // Whether to save structured files
    if (!service::ServiceRegistry::Instance().Get<service::IOverviewConfig>().GetOverviewStructureFile()) {
        return;
    }

    auto path = cosmo::path::GetTaskOverviewDataPath(taskId);
    std::error_code err;
    std::filesystem::remove_all(path, err);
}

DataDetTrackClassifyPtr GenRandomDetBoxs() {
    DataDetTrackClassifyPtr input = std::make_shared<DataDetTrackClassify>();

    thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> countDist(1, 10);
    std::uniform_int_distribution<int> xDist(1, 900);
    std::uniform_int_distribution<int> yDist(1, 500);
    std::uniform_int_distribution<int> sizeDist(32, 331);

    int randomNumber = countDist(rng);
    for (int i = 0; i < randomNumber; i++) {
        AiDetectRstEl ret;
        ret.box.x      = xDist(rng);
        ret.box.y      = yDist(rng);
        ret.box.width  = sizeDist(rng);
        ret.box.height = sizeDist(rng);
        TargetAreaUnit area;
        area.area_id   = "test";
        area.area_name = "test";
        ret.areaSign.areas.push_back(area);
        ret.bFilter = false;
        input->targets.push_back(ret);
    }

    return input;
}
}  // namespace cosmo