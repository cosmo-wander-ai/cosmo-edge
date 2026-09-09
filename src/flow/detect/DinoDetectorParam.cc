// DinoDetectorParam.cc — Parameter and task management for DinoDetector.
// Split from DinoDetector.cc to reduce file size (DEBT-007).

#include <unistd.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <thread>
#include <tuple>

#include "flow/common/AlgDataRecord.h"
#include "flow/detect/DinoDetector.h"
#include "media/VideoFrame.h"
#include "service/detail/ServiceRegistry.h"
#include "util/GeometricPos.h"
#include "util/Log.h"
#include "util/TimeUtil.h"
#include "util/TimingConstants.h"
#include "util/UuidUtil.h"
#include "util/dto/ActionCodes.h"

static constexpr const char* kTag = "DINO-DETECTER ";
namespace cosmo {

namespace {

    struct DinoInferenceKey {
        std::string prompt;
        float text_threshold = 0.25f;
        float box_threshold  = 0.3f;

        bool operator<(const DinoInferenceKey& other) const {
            return std::tie(prompt, text_threshold, box_threshold) <
                   std::tie(other.prompt, other.text_threshold, other.box_threshold);
        }
    };

}  // namespace

bool DinoDetector::TaskExist(const std::string& channel_id, const std::string& task) const {
    std::lock_guard<std::shared_mutex> lock(mtx);
    return std::any_of(channel_list_.begin(), channel_list_.end(), [&channel_id, &task](const auto& ch) {
        if (ch.channel != channel_id)
            return false;
        return std::any_of(ch.tasks.begin(), ch.tasks.end(), [&task](const auto& t) { return t == task; });
    });
}

bool DinoDetector::TaskIsFull() const {
    std::lock_guard<std::shared_mutex> lock(mtx);
    return channel_list_.size() >= max_reuse_count_;
}

bool DinoDetector::TaskIsEmpty() const {
    std::lock_guard<std::shared_mutex> lock(mtx);
    return channel_list_.empty();
}

size_t DinoDetector::ChannelCount() const {
    std::lock_guard<std::shared_mutex> lock(mtx);
    return channel_list_.size();
}

size_t DinoDetector::TaskCount() const {
    std::lock_guard<std::shared_mutex> lock(mtx);
    size_t count = 0;
    for (auto& ch : channel_list_) {
        count += ch.tasks.size();
    }
    return count;
}

DinoDetectorParamEl DinoDetector::FoundLocalParamByTask(const AlgTaskUnit& task) const {
    DinoDetectorParamEl param;
    param.task_id = task.task_id;
    return param;
}

DinoDetectorParamEl DinoDetector::GetTaskParamsUnlocked(const std::string& taskId) const {
    auto it = std::find_if(params_.param.begin(), params_.param.end(),
                           [&taskId](const auto& param) { return param.task_id == taskId; });
    if (it != params_.param.end()) {
        return *it;
    }
    DinoDetectorParamEl default_param;
    default_param.task_id = taskId;
    return default_param;
}

DinoDetectorParamEl DinoDetector::GetTaskParams(const std::string& taskId) const {
    std::lock_guard<std::shared_mutex> lock(mtx);
    return GetTaskParamsUnlocked(taskId);
}

void DinoDetector::HandFrames(std::vector<AlgDataPtr> alg_datas) {
    if (!detector_) {
        action_status = util::ErrorEnum::AI_INST_NOTCREATED;
        if (!DinoSdkInit())
            return;
    }

    // A channel frame may feed several tasks.  Build inference groups by the
    // complete prompt/threshold contract, then select the corresponding result
    // in DistributorData's per-task callback.  This prevents one task from
    // inheriting another task's prompt or thresholds.
    std::vector<VideoFramePtr> images;
    std::vector<AlgDataPtr> validDatas;
    std::vector<std::vector<std::string>> frameTasks;

    for (auto& data : alg_datas) {
        if (!data || !data->chanDataDec.frame || !data->chanDataDec.frame->Active()) {
            continue;
        }
        std::vector<std::string> task_ids;
        if (!data->taskId.empty()) {
            task_ids.push_back(data->taskId);
        } else {
            std::shared_lock<std::shared_mutex> lockCh(mtx);
            auto it = std::find_if(channel_list_.begin(), channel_list_.end(), [&data](const auto& ch) {
                return ch.channel == data->channelId && !ch.tasks.empty();
            });
            if (it != channel_list_.end())
                task_ids = it->tasks;
        }
        if (task_ids.empty()) {
            LOG_WARN("{}[{} {}] Channel:{} has no DINO task binding", kTag, alg_code_, uuid, data->channelId);
            continue;
        }

        images.push_back(data->chanDataDec.frame);
        validDatas.push_back(data);
        frameTasks.push_back(std::move(task_ids));
    }

    if (images.empty()) {
        return;
    }

    using FrameConsumers = std::map<size_t, std::vector<std::string>>;
    std::map<DinoInferenceKey, FrameConsumers> inference_groups;
    for (size_t input_index = 0; input_index < frameTasks.size(); ++input_index) {
        for (const auto& consumer_task_id : frameTasks[input_index]) {
            const auto task_param = GetTaskParams(consumer_task_id);
            DinoInferenceKey key;
            key.prompt         = task_param.prompt.empty() ? "person" : task_param.prompt;
            key.text_threshold = task_param.text_confidence;
            key.box_threshold  = task_param.box_confidence;
            inference_groups[key][input_index].push_back(consumer_task_id);
        }
    }

    std::vector<std::map<std::string, std::vector<AiDetectRstEl>>> results_by_task(validDatas.size());

    for (const auto& group : inference_groups) {
        std::vector<VideoFramePtr> groupImages;
        std::vector<size_t> frame_indices;
        groupImages.reserve(group.second.size());
        frame_indices.reserve(group.second.size());
        for (const auto& frame : group.second) {
            frame_indices.push_back(frame.first);
            groupImages.push_back(images.at(frame.first));
        }
        std::vector<std::vector<AiDetectRstEl>> groupResults;
        DinoDetectionOptions options;
        options.text_threshold = group.first.text_threshold;
        options.box_threshold  = group.first.box_threshold;
        auto ret               = detector_->Detect(groupImages, group.first.prompt, options, groupResults);
        if (util::ErrorEnum::Success != ret) {
            LOG_WARN("{}[{} {}] Detect Failed. prompt:\"{}\" text:{} box:{} Ret:{}", kTag, alg_code_, uuid,
                     group.first.prompt, group.first.text_threshold, group.first.box_threshold, ret);
            continue;
        }
        for (size_t result_index = 0;
             result_index < frame_indices.size() && result_index < groupResults.size(); ++result_index) {
            const size_t input_index = frame_indices[result_index];
            const auto consumers_it  = group.second.find(input_index);
            if (consumers_it == group.second.end())
                continue;
            for (const auto& consumer_task_id : consumers_it->second)
                results_by_task[input_index][consumer_task_id] = groupResults[result_index];
        }
    }

    // Write detection results back in original order and distribute
    for (size_t i = 0; i < validDatas.size(); i++) {
        auto& data                               = validDatas[i];
        data->dataType                           = AlgDataType::ChannelDataDetect;
        data->chanDataDetect.atomicCode          = alg_code_;
        data->chanDataDetect.detRet              = std::make_shared<DataDetTrackClassify>();
        data->chanDataDetect.detRet->streamIndex = images[i]->GetStreamIndex();
        data->chanDataDetect.detRet->frameIndex  = images[i]->GetFrameIndex();
        data->chanDataDetect.detRet->timestamp   = images[i]->GetTimestamp();
        data->chanDataDetect.detRet->picWidth    = images[i]->GetWidth();
        data->chanDataDetect.detRet->picHeight   = images[i]->GetHeight();
        data->chanDataDetect.detRet->targets.clear();

        const auto task_results = results_by_task[i];
        distributor->DistributorData(
            data->channelId, data, [this, task_results](AlgDataPtr frame, const std::string& taskId) {
                auto outData = AlgDataCopy(frame);
                if (!outData) {
                    return frame;
                }
                outData->taskId = taskId;

                if (outData->chanDataDetect.detRet) {
                    const auto result = task_results.find(taskId);
                    outData->chanDataDetect.detRet->targets =
                        result == task_results.end() ? std::vector<AiDetectRstEl>{} : result->second;
                }

                SignTargetAreas(outData, taskId);
                return outData;
            });
    }

    LOG_DEBUG("{}[{} {}] Processed {} frames ({} DINO inference contracts)", kTag, alg_code_, uuid,
              validDatas.size(), inference_groups.size());
}

void DinoDetector::TargetAddArea(AiDetectRstEl& target, TargetPosition pos, TargetAreaType type,
                                 MsgTaskArea& area, int pic_width, int pic_height, bool associated_area,
                                 const std::string& main_area_id) {
    if (BoxInArea(target.box, pos, target.point, area.points, pic_width, pic_height)) {
        TargetAreaUnit targetArea;
        targetArea.area_id            = area.areaId;
        targetArea.area_name          = area.name;
        targetArea.is_associated_area = associated_area;
        targetArea.main_area_id       = main_area_id;
        targetArea.position           = pos;
        targetArea.type               = type;
        target.targetPos              = pos;
        if (TargetAreaType::kNormal == type) {
            target.areaSign.areas.push_back(targetArea);
        } else {
            target.areaSign.shielded_areas.push_back(targetArea);
        }
    }
}

void DinoDetector::TargetAddLine(AiDetectRstEl& target, TargetPosition pos, MsgTaskArea& area, int pic_width,
                                 int pic_height, bool associated_area, const std::string& main_area_id) {
    TargetLineUnit targetOnLineUnit;
    targetOnLineUnit.area_id            = area.areaId;
    targetOnLineUnit.area_name          = area.name;
    targetOnLineUnit.is_associated_area = associated_area;
    targetOnLineUnit.main_area_id       = main_area_id;
    targetOnLineUnit.position           = pos;
    targetOnLineUnit.type =
        BoxOnLinePos(target.box, pos, target.point, area.linePoints, pic_width, pic_height);
    target.targetPos = pos;
    target.areaSign.lines.push_back(targetOnLineUnit);
}

void DinoDetector::SignTargetAreas(AlgDataPtr data_ptr, const std::string& taskId) {
    if (!data_ptr || !data_ptr->chanDataDec.frame || !data_ptr->chanDataDetect.detRet) {
        return;
    }

    auto detRet = data_ptr->chanDataDetect.detRet;
    auto width  = data_ptr->chanDataDec.frame->GetWidth();
    auto height = data_ptr->chanDataDec.frame->GetHeight();

    std::shared_lock<std::shared_mutex> lock(mtx);
    auto taskAreaIt = task_areas_.find(taskId);
    if (taskAreaIt != task_areas_.end()) {
        auto& taskArea = taskAreaIt->second;
        if (!taskArea.areas.empty()) {
            detRet->bHaveArea = true;
        }
        if (!taskArea.shieldedAreas.empty()) {
            detRet->bHaveShieldedArea = true;
        }

        for (auto& target : detRet->targets) {
            auto pos = TargetPosition::kBottom;
            for (auto area : taskArea.areas) {
                if (!area.points.empty()) {
                    TargetAddArea(target, pos, TargetAreaType::kNormal, area, width, height, false, "");
                }
                if (!area.linePoints.empty()) {
                    TargetAddLine(target, pos, area, width, height, false, "");
                }
                for (auto assArea : area.associatedAreas) {
                    if (!assArea.points.empty()) {
                        TargetAddArea(target, pos, TargetAreaType::kNormal, assArea, width, height, true,
                                      area.areaId);
                    }
                    if (!assArea.linePoints.empty()) {
                        TargetAddLine(target, pos, assArea, width, height, true, area.areaId);
                    }
                }
            }

            for (auto shieldedArea : taskArea.shieldedAreas) {
                if (!shieldedArea.points.empty()) {
                    TargetAddArea(target, pos, TargetAreaType::kShield, shieldedArea, width, height);
                }
                for (auto assArea : shieldedArea.associatedAreas) {
                    if (!assArea.points.empty()) {
                        TargetAddArea(target, pos, TargetAreaType::kShield, assArea, width, height, true,
                                      shieldedArea.areaId);
                    }
                    if (!assArea.linePoints.empty()) {
                        TargetAddLine(target, pos, assArea, width, height, true, shieldedArea.areaId);
                    }
                }
            }
        }
    }

    OverviewRecord(taskId, detRet);
}

void DinoDetector::AddOverviewTask(const std::string& taskId) {
    auto it = overview_rec_insts_.find(taskId);
    if (it == overview_rec_insts_.end()) {
        auto recordInst             = std::make_shared<OverviewRecordAiRst>(taskId, "detect_dino");
        overview_rec_insts_[taskId] = recordInst;
    }
}

void DinoDetector::OverviewRecord(const std::string& taskId, DataDetTrackClassifyPtr det_ret) {
    auto it                                 = overview_rec_insts_.find(taskId);
    static int64_t s_lastOverviewWriteLogTs = 0;
    auto now                                = util::GetMilliseconds();
    if ((now - s_lastOverviewWriteLogTs) > 1000) {
        s_lastOverviewWriteLogTs = now;
        int targetCnt            = (det_ret ? static_cast<int>(det_ret->targets.size()) : -1);
        int64_t frameIndex       = det_ret ? det_ret->frameIndex : -1;
        int64_t frame_ts         = det_ret ? det_ret->timestamp : -1;
        LOG_INFO("[DINO_OV_WRITE][{}:{}] taskId:{} frame:{} ts:{} targets:{} overviewInst:{}", alg_code_,
                 uuid, taskId, frameIndex, frame_ts, targetCnt,
                 (it != overview_rec_insts_.end() && it->second) ? 1 : 0);
    }
    if (it != overview_rec_insts_.end() && it->second) {
        it->second->OverviewRecordFrame(det_ret);
    }
}

MsgOverviewMem DinoDetector::GetOverviewInfo(const std::string& /*channel_id*/, const std::string& taskId,
                                             int64_t streamIndex, int64_t from, int64_t to) {
    MsgOverviewMem info;
    auto it = overview_rec_insts_.find(taskId);
    if (it != overview_rec_insts_.end() && it->second) {
        info = it->second->GetOverviewInfo(streamIndex, from, to);
    }
    return info;
}

void DinoDetector::run() {
    LOG_INFO("{}[{} {}] Thread Start", kTag, alg_code_, uuid);
    int index = 0;
    while (running) {
        if ((data_queue->RestSize() >= batch_count_) || ((index >= 100) && (data_queue->RestSize() > 0))) {
            size_t detnum     = data_queue->RestSize();
            auto inputFpsCalc = input_fps_calc.FpsWithFrame();
            handle_frame_cnt  = inputFpsCalc.first;
            in_fps            = inputFpsCalc.second;
            std::vector<AlgDataPtr> algDatas;
            for (size_t i = 0; i < detnum && i < batch_count_; i++) {
                auto data = data_queue->Pop();
                if (data) {
                    algDatas.push_back(data);
                }
            }
            if (!algDatas.empty()) {
                duration_stat.BeginSample();
                HandFrames(algDatas);
                duration_stat.EndSample();
            }
            index = 0;
        } else {
            std::this_thread::sleep_for(timing::kFastPollInterval);
            index += 1;
        }
    }
    // Release model on thread exit to free VRAM (HandFrames->DinoSdkInit will reload on next start)
    if (detector_) {
        LOG_INFO("{}[{} {}] Thread exiting, releasing Dino model to free VRAM", kTag, alg_code_, uuid);
        detector_.reset();
        detector_              = nullptr;
        is_detector_inst_init_ = false;
    }
    LOG_INFO("{}[{} {}] Thread Stop", kTag, alg_code_, uuid);
}

}  // namespace cosmo
