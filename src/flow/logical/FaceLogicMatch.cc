// FaceLogicMatch.cc — face matching.
// Split from FaceLogic.cc (DEBT-007).

/*
 * @Author: zhangxiaobo
 * @Date: 2023-11-29 15:55:39
 * @LastEditors: zhangxiaobo
 * @LastEditTime: 2025-08-18 10:08:27
 * @Description:
 */

#include <chrono>
#include <thread>

#include "flow/logical/FaceLogic.h"
#include "flow/logical/FaceLogicTypes.h"
#include "service/detail/ServiceRegistry.h"
#include "service/system/IConfigReadService.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/TimeUtil.h"
#include "util/TimingConstants.h"
#include "util/UuidUtil.h"

static constexpr const char *kTag     = "CaptureLogic ";
static constexpr int kMillisPerSecond = 1000;
static constexpr size_t kLogInterval  = 100;

namespace cosmo {

void FaceLogic::AddHistory(AlgDataPtr algData, VideoFramePtr frame, DataDetTrackClassifyPtr input,
                           const std::chrono::steady_clock::time_point &dataTimePoint) {
    for (auto &target : input->targets) {
        auto &idData       = track_id_status_map_[target.trackId];
        idData.track_id    = target.trackId;
        idData.is_detected = true;

        if (idData.track_id_uuid.empty())  // Appears for the first time
        {
            idData.track_id_uuid           = target.trackIdInfo;
            idData.real_time_pic_timepoint = dataTimePoint;  // First timepoint when reporting in real-time
            idData.last_report_timepoint   = dataTimePoint;
            LOG_INFO("{}[{}] track_id:{} Appear", kTag, task_id, target.trackId);
            RecordIntoArea(idData, target, frame);
        } else {
            RecordIntoArea(idData, target, frame);
            HandOutArea(algData, idData, target, frame);
        }
        if (FaceLogicCaptureStrategy::kRealtime == params_.capture_strategy) {
            HandRealtimeFace(algData, idData, dataTimePoint);
        }
        idData.det_rst   = target;
        idData.pic_frame = frame;

        if (input->bHaveArea)  // When there is an area
        {
            if (!target.areaSign.shielded_areas.empty()) {
                continue;
            }
            if (target.areaSign.areas.empty()) {
                continue;
            }
        }
        if (target.bFilter) {
            continue;
        }
        float quality   = 0.0;
        FaceAngle angle = FaceAngle::FaceAngleMax;
        GetTargteQualityAngle(algData->bHaveRelated, target, quality, angle);
        if (!FindAngleInConfig(angle)) {
            continue;
        }
        if (quality > idData.real_time_pic_max_quality) {
            LOG_INFO("{}[{}] track_id:{} Quality From {} To {}", kTag, task_id, idData.track_id,
                     idData.real_time_pic_max_quality, quality);
            idData.real_time_pic_max_quality = quality;
            idData.real_time_pic.pic_frame   = frame;
            idData.real_time_pic.pic_info    = target;
            idData.real_time_pic.is_init     = true;
        }

        if (quality > idData.best_pic_max_quality) {
            idData.best_pic_max_quality = quality;
            idData.best_pic.pic_frame   = frame;
            idData.best_pic.pic_info    = target;
            idData.best_pic.is_init     = true;
        }
    }
}

void FaceLogic::OldTrackId(AlgDataPtr dataPtr) {
    for (auto it = track_id_status_map_.begin(); it != track_id_status_map_.end();) {
        if (!it->second.is_detected) {
            if (det_data_status_.bHaveArea) {
                HandOutAreaWithDisapear(dataPtr, it->second, dataPtr->chanDataDec.frame);
            } else {
                HandBestFace(
                    dataPtr, it->second, std::string(key::DEFAULT_AREA),
                    std::string(
                        key::DEFAULT_AREA_NAME));  // Need to check when the real-time snapshot disappears
            }

            LOG_INFO("{}[{}] track_id:{} Disappear", kTag, task_id, it->second.track_id);
            it = track_id_status_map_.erase(it);
        } else {
            it->second.is_detected = false;
            ++it;
        }
    }
}

bool FaceLogic::GetQualityAngle(AiDetectRstEl & /*target*/, const std::vector<AiConfidence> &classifyRst,
                                float &quality, FaceAngle &angle, const util::Box &box) {
    AiConfidence faceQuality;
    AiConfidence faceAngle;
    // Face quality
    if (GetFaceQuality(classifyRst, faceQuality)) {
        GetFaceAngle(classifyRst, faceAngle);
        GetFaceAngleType(faceAngle, angle);
    }
    // If face quality not is_detected, detect body quality (body snapshot logic)
    else if (GetPedQuality(classifyRst, faceQuality)) {
        angle = FaceAngle::FaceAngleFront;
    }
    // For license plate recognition, use size as quality score
    else {
        angle                  = FaceAngle::FaceAngleFront;
        faceQuality.confidence = box.width * box.height;
    }

    quality = faceQuality.confidence;

    return true;
}

bool FaceLogic::GetTargteQualityAngle(bool bHaveRelated, AiDetectRstEl &target, float &quality,
                                      FaceAngle &angle) {
    if (bHaveRelated) {
        if (target.relatedEl.bActive) {
            return GetQualityAngle(target, target.relatedEl.classifyRst, quality, angle,
                                   target.relatedEl.box);
        }

        return false;
    }

    return GetQualityAngle(target, target.classifyRst, quality, angle, target.box);
}

void FaceLogic::PushData(AlgDataPtr dataPtr, TrackIdData &track_id_data, const std::string &tag,
                         OnEventsReportType reportType, const std::string &areaId,
                         const std::string &area_name) {
    AlgDataPtr algData = AlgDataCopy(dataPtr);
    if (!algData)
        return;

    algData->dataType  = AlgDataType::TaskDataFaceLogic;
    algData->channelId = dataPtr->channelId;
    algData->taskId    = dataPtr->taskId;

    auto faceResult = std::make_shared<DataDetTrackClassify>();
    algData->SetTaskResult(AlgDataType::TaskDataFaceLogic, faceResult);
    faceResult->frameIndex        = det_data_status_.frameIndex;
    faceResult->streamIndex       = det_data_status_.streamIndex;
    faceResult->timestamp         = det_data_status_.timestamp;
    faceResult->picWidth          = det_data_status_.picWidth;
    faceResult->picHeight         = det_data_status_.picHeight;
    faceResult->bHaveArea         = det_data_status_.bHaveArea;
    faceResult->bHaveShieldedArea = det_data_status_.bHaveShieldedArea;
    faceResult->dataType          = AlgDataType::TaskDataFaceLogic;
    faceResult->reportType        = reportType;
    faceResult->areaInfo.areaId   = areaId;
    faceResult->areaInfo.areaName = area_name;
    const auto &intoArea          = track_id_data.into_area_pics[areaId];
    if ((intoArea.is_init) && (params_.in_out_picture)) {
        if (intoArea.pic_frame) {
            faceResult->areaInfo.bHaveIntoArea  = true;
            faceResult->areaInfo.intoAreaFrame  = intoArea.pic_frame;
            faceResult->areaInfo.intoAreaTarget = intoArea.pic_info;
        }
    }
    if (OnEventsReportType::Realtime == reportType) {
        algData->chanDataDec.reportTimeStamp = dataPtr->chanDataDec.frame->GetTimestamp();
        algData->chanDataDec.frame           = track_id_data.real_time_pic.pic_frame;  // Original picture
        faceResult->targets.push_back(track_id_data.real_time_pic.pic_info);
        LOG_INFO(
            "{}[{}] track_id:{} Area:{} relatedEl:{} [{}] Quality:{} At RealFrame:{}/EventFrame:{} Time:{}",
            kTag, task_id, track_id_data.real_time_pic.pic_info.trackId, areaId,
            track_id_data.real_time_pic.pic_info.relatedEl.bActive, tag,
            track_id_data.real_time_pic_max_quality, det_data_status_.frameIndex,
            track_id_data.real_time_pic.pic_frame->GetFrameIndex(), det_data_status_.timestamp);
    } else {
        algData->chanDataDec.reportTimeStamp = dataPtr->chanDataDec.frame->GetTimestamp();
        algData->chanDataDec.frame           = track_id_data.best_pic.pic_frame;  // Original picture
        faceResult->targets.push_back(track_id_data.best_pic.pic_info);

        if ((intoArea.is_init) && (params_.in_out_picture)) {
            faceResult->areaInfo.bHaveOutArea  = true;
            faceResult->areaInfo.outAreaFrame  = track_id_data.pic_frame;
            faceResult->areaInfo.outAreaTarget = track_id_data.det_rst;
            LOG_INFO(
                "{}[{}] track_id:{} Area:{} relatedEl:{} [{}] Quality:{} At RealFrame:{}/EventFrame:{} "
                "Time:{}",
                kTag, task_id, track_id_data.real_time_pic.pic_info.trackId, areaId,
                track_id_data.real_time_pic.pic_info.relatedEl.bActive, tag,
                track_id_data.best_pic_max_quality, det_data_status_.frameIndex,
                track_id_data.best_pic.pic_frame->GetFrameIndex(), det_data_status_.timestamp);
        } else {
            LOG_WARN(
                "{}[{}] track_id:{} Area:{} relatedEl:{} [{}] Quality:{} At RealFrame:{}/EventFrame:{} "
                "Time:{} But Have No into Pic",
                kTag, task_id, track_id_data.real_time_pic.pic_info.trackId, areaId,
                track_id_data.real_time_pic.pic_info.relatedEl.bActive, tag,
                track_id_data.best_pic_max_quality, det_data_status_.frameIndex,
                track_id_data.best_pic.pic_frame->GetFrameIndex(), det_data_status_.timestamp);
        }
    }

    distributor->DistributorData(algData);
}

void FaceLogic::HandBestFace(AlgDataPtr dataPtr, TrackIdData &track_id_data, const std::string &areaId,
                             const std::string &area_name) {
    if (track_id_data.best_pic_max_quality < params_.capture_quality) {
        return;
    }

    PushData(dataPtr, track_id_data, "BestCap", OnEventsReportType::Trigger, areaId, area_name);
}

void FaceLogic::HandRealtimeFace(AlgDataPtr dataPtr, TrackIdData &track_id_data,
                                 const std::chrono::steady_clock::time_point &dataTimePoint) {
    constexpr int kMillisPerSecond = 1000;
    if ((std::chrono::duration_cast<std::chrono::milliseconds>(dataTimePoint -
                                                               track_id_data.real_time_pic_timepoint)
             .count() > (kMillisPerSecond * params_.realtime_duration))) {
        if (track_id_data.real_time_pic_max_quality < params_.capture_quality) {
            LOG_INFO(
                "{}[{}] track_id:{}/{}/{} relatedEl:{}  [RealtimeCap] Quality:{} < {} At RealFrame:{} "
                "Time:{}",
                kTag, task_id, track_id_data.track_id, track_id_data.real_time_pic.is_init,
                track_id_data.real_time_pic.pic_info.trackId,
                track_id_data.real_time_pic.pic_info.relatedEl.bActive,
                track_id_data.real_time_pic_max_quality, params_.capture_quality, det_data_status_.frameIndex,
                det_data_status_.timestamp);
        } else {
            if (det_data_status_.bHaveArea) {
                for (auto &lastArea : track_id_data.into_area_pics) {
                    PushData(dataPtr, track_id_data, "RealtimeCap", OnEventsReportType::Realtime,
                             lastArea.first, lastArea.second.area_name);
                }
            } else {
                PushData(dataPtr, track_id_data, "RealtimeCap", OnEventsReportType::Realtime, "", "");
            }
        }

        track_id_data.has_report                = true;
        track_id_data.real_time_pic_max_quality = -1.0;
        track_id_data.real_time_pic_timepoint   = dataTimePoint;
        track_id_data.last_report_timepoint     = dataTimePoint;
    }
}

void FaceLogic::HandFrame(AlgDataPtr algData) {
    if (!algData) {
        filter_frames_ += 1;
        if (0 == filter_frames_ % kLogInterval) {
            LOG_WARN("{}[{}] Filter {} Frames", kTag, task_id, filter_frames_);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    if (!((AlgDataType::ChannelDataDetect == algData->dataType) ||
          (AlgDataType::TaskDataTrack == algData->dataType) ||
          (AlgDataType::TaskDataClassify == algData->dataType) ||
          (AlgDataType::TaskDataPersonFace == algData->dataType))) {
        filter_frames_ += 1;
        if (0 == filter_frames_ % kLogInterval) {
            LOG_WARN("{}[{}] Filter {} Frames", kTag, task_id, filter_frames_);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    DataDetTrackClassifyPtr input;
    if (AlgDataType::ChannelDataDetect == algData->dataType) {
        input = algData->chanDataDetect.detRet;
    } else if ((AlgDataType::TaskDataTrack == algData->dataType) ||
               (AlgDataType::TaskDataPersonFace == algData->dataType)) {
        input = algData->GetTaskResult(AlgDataType::TaskDataTrack);
    } else {
        input = algData->GetTaskResult(AlgDataType::TaskDataClassify);
    }
    det_data_status_.bHaveArea         = input->bHaveArea;
    det_data_status_.bHaveShieldedArea = input->bHaveShieldedArea;
    det_data_status_.streamIndex       = input->streamIndex;
    det_data_status_.frameIndex        = input->frameIndex;
    det_data_status_.timestamp         = input->timestamp;
    det_data_status_.picWidth          = input->picWidth;
    det_data_status_.picHeight         = input->picHeight;

    AddHistory(algData, algData->chanDataDec.frame, input, algData->firstTimePoint);
    OldTrackId(algData);

    handle_frame_cnt += 1;
    if (0 == handle_frame_cnt % kLogInterval) {
        LOG_INFO("{}[{}] Handle {} Frames", kTag, task_id, handle_frame_cnt);
    }
    action_status = util::ErrorEnum::Success;
    return;
}

void FaceLogic::run() {
    while (running) {
        if (data_queue->RestSize() > 0) {
            auto decData = data_queue->Pop();
            if (decData) {
                if (service::ServiceRegistry::Instance().Get<service::IConfigReadService>().GetActionSwitch(
                        GetActionId())) {
                    HandFrame(decData);
                }
            }
        } else {
            std::this_thread::sleep_for(timing::kMediumPollInterval);
        }
    }

    LOG_INFO("[{}] THREAD [{}] Stop ", task_id, Name());
}

}  // namespace cosmo
