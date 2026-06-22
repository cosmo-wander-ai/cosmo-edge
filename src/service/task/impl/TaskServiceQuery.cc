// TaskServiceQuery.cc — Task query and status for TaskServiceImpl.
// Split from TaskServiceImpl.cc to reduce file size (DEBT-007).

#include <mutex>
#include <string_view>

#include "flow/channel/AlgChannel.h"
#include "flow/task/TaskBase.h"
#include "flow/task/TaskBaseParam.h"
#include "service/detail/ServiceRegistry.h"
#include "service/task/impl/TaskServiceImpl.h"
#include "util/Log.h"
#include "util/TimeUtil.h"
#include "util/dto/ActionCodes.h"
#include "util/dto/CameraMsgTypes.h"

namespace cosmo::service {

std::vector<cosmo::MsgCameraInfo> TaskServiceImpl::CameraTaskInfo() {
    std::vector<cosmo::MsgCameraInfo> camera_infos;
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (const auto& kv : tasks_) {
        const auto& task = kv.second;
        if (!task)  // Skip tasks that failed to create
        {
            continue;
        }

        auto it = std::find_if(camera_infos.begin(), camera_infos.end(), [&](const auto& camera_info) {
            return camera_info.videoChannelId == task->channelId;
        });
        if (it != camera_infos.end()) {
            cosmo::MsgCameraTask camera_task;
            camera_task.algorithmId   = task->GetAlgId();
            camera_task.algorithmName = task->GetAlgName();
            camera_task.enable        = 1;
            camera_task.status        = 1;
            it->taskList.push_back(camera_task);
        } else {
            cosmo::MsgCameraInfo camera;
            camera.videoChannelId = task->channelId;
            camera.channelName    = task->channelName;
            cosmo::MsgCameraTask camera_task;
            camera_task.algorithmId   = task->GetAlgId();
            camera_task.algorithmName = task->GetAlgName();
            camera_task.enable        = 1;
            camera_task.status        = 1;
            camera.taskList.push_back(camera_task);
            camera_infos.push_back(camera);
        }
    }

    return camera_infos;
}

std::vector<cosmo::DataDetTrackClassify> TaskServiceImpl::GetTaskDetHistory(const std::string& channel_id,
                                                                            const std::string& taskId,
                                                                            int64_t from, int64_t timestamp,
                                                                            int64_t to) {
    std::vector<cosmo::DataDetTrackClassify> history;
    std::shared_lock<std::shared_mutex> lock(mtx_);
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) {
        LOG_WARN("Channel:{} Task:{} Not In Pool, Cant GetParam.", channel_id, taskId);
        return {};
    }
    auto task = it->second;
    // Task not started or already stopped
    if (!task->is_started) {
        return {};
    }

    for (auto& action : task->actions) {
        if (action.actionInst) {
            auto unit_history = action.actionInst->GetHistory(channel_id, taskId, from, timestamp, to);
            if (!unit_history.empty())
                history.insert(history.end(), unit_history.begin(), unit_history.end());
        }
    }

    return history;
}

std::vector<cosmo::MsgOverviewMem> TaskServiceImpl::GetTaskLiveOverviewInfo(const std::string& taskId,
                                                                            int64_t stream_index,
                                                                            int64_t from, int64_t to) {
    std::vector<cosmo::MsgOverviewMem> infos;
    int ai_data_cnt = 0, ai_frame_cnt = 0, ai_target_cnt = 0, alarm_data_cnt = 0;
    std::shared_lock<std::shared_mutex> lock(mtx_);
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) {
        // Live preview polls at high frequency when task does not exist; rate-limit by taskId to avoid log
        // flooding
        auto now      = cosmo::util::GetMilliseconds();
        bool need_log = false;
        {
            std::lock_guard<std::mutex> lk(log_throttle_mtx_);
            auto& last = not_in_pool_log_ts_[taskId];
            if ((now - last) > 10 * 1000) {
                last     = now;
                need_log = true;
            }
        }
        if (need_log) {
            LOG_WARN("Task:{} Not In Pool, Cant GetParam.", taskId);
        }
        return {};
    }
    auto task = it->second;
    // Task not started or already stopped
    if (!task->is_started) {
        return {};
    }

    for (auto& action : task->actions) {
        if (action.actionInst) {
            auto unit_info =
                action.actionInst->GetOverviewInfo(task->channelId, taskId, stream_index, from, to);
            if (cosmo::MsgOverviewMemDataType::MsgOverviewMemDataTypeNone != unit_info.type) {
                if (unit_info.type == cosmo::MsgOverviewMemDataType::MsgOverviewMemDataTypeAIData) {
                    ai_data_cnt++;
                    ai_frame_cnt += unit_info.aiFrames.size();
                    for (auto& ai_frame : unit_info.aiFrames) {
                        ai_target_cnt += ai_frame.targets.size();
                    }
                } else if (unit_info.type == cosmo::MsgOverviewMemDataType::MsgOverviewMemDataTypeAlarm) {
                    alarm_data_cnt++;
                }
                infos.push_back(unit_info);
            }
        }
    }

    cosmo::MsgOverviewMem unit_info;
    unit_info.type   = cosmo::MsgOverviewMemDataType::MsgOverviewMemDataTypeParams;
    unit_info.params = task->params;
    infos.push_back(unit_info);
    auto now = cosmo::util::GetMilliseconds();
    if ((now - last_overview_log_ts_) > 1000) {
        last_overview_log_ts_ = now;
        LOG_INFO("[TASK_OV_READ] taskId:{} channel:{} infos:{} aiData:{} aiFrames:{} aiTargets:{} alarm:{}",
                 taskId, task->channelId, infos.size(), ai_data_cnt, ai_frame_cnt, ai_target_cnt,
                 alarm_data_cnt);
    }
    return infos;
}

cosmo::TaskAlarmPtr TaskServiceImpl::GetAlarmInst(const std::string& channel_id, const std::string& taskId) {
    return task_base_->GetAlarmInst(channel_id, taskId);
}

cosmo::AlgChannelPtr TaskServiceImpl::GetChannelInst(const std::string& channel_id) {
    return task_base_->GetChannelInst(channel_id);
}

std::vector<std::string> TaskServiceImpl::GetChannelTasks(const std::string& channel_id) {
    return task_base_->GetChannelTasks(channel_id);
}

std::string TaskServiceImpl::GetTaskChannel(const std::string& taskId) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) {
        LOG_WARN("Task:{} Not In Pool, Cant Get Channel.", taskId);
        return "";
    }
    return it->second->channelId;
}

std::string TaskServiceImpl::GetTaskAlgId(const std::string& taskId) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) {
        LOG_WARN("Task:{} Not In Pool, Cant Get Channel.", taskId);
        return "";
    }
    return it->second->GetAlgId();
}

bool TaskServiceImpl::GetTaskFrameInfo(const std::string& taskId, bool& bLive, int64_t& index, int64_t& pts,
                                       int64_t& frameSize, std::string& streamUrl) {
    std::string channel_id = GetTaskChannel(taskId);
    auto channel_inst      = GetChannelInst(channel_id);
    if (channel_inst) {
        return channel_inst->GetFrameInfo(bLive, index, pts, frameSize, streamUrl);
    }
    return false;
}

bool TaskServiceImpl::GetChannelAttr(const std::string& channel_id, cosmo::MsgCameraAttr& attr) {
    auto channel_inst = GetChannelInst(channel_id);
    if (channel_inst) {
        return channel_inst->GetAttr(attr);
    }
    return false;
}

bool TaskServiceImpl::TaskDataActive(const std::string& channel_id) {
    auto channel_inst = GetChannelInst(channel_id);
    if (channel_inst && channel_inst->IsDataActive()) {
        return true;
    }

    // Demuxer says no active data (e.g. offline file EOF), but we must ensure downstream memory queues are
    // fully consumed Check if any frame is actively queued OR being processed in any threads
    auto channel_tasks = GetChannelTasks(channel_id);
    auto stats         = GetTaskStatus(channel_tasks, 1);
    return std::any_of(stats.begin(), stats.end(), [](const auto& stat) {
        return std::any_of(stat.queStatus.begin(), stat.queStatus.end(), [](const auto& qs) {
            return qs.queueStatus.insertCount > (qs.queueStatus.processCount + qs.queueStatus.discardCount);
        });
    });
}

void TaskServiceImpl::ShowActions(cosmo::ActionAlgPtr actionAlg) {
    if (!actionAlg) {
        return;
    }
    LOG_INFO("{}", "====================ALGORITHM ACTIONs====================");
    LOG_INFO("[{}/{}] UpdateTime:{} CheckSum:{}", actionAlg->algorithmCode, actionAlg->algorithmName,
             actionAlg->algorithmUpdateTime, actionAlg->algorithmCheckSum);
    for (auto& work_flow : actionAlg->workFlow) {
        LOG_INFO("ACTION:[{}/{}] Flow:{} PreFlow:{}", work_flow.actionId, work_flow.actionName,
                 work_flow.flowActionId, work_flow.preFlowActionId);
        for (auto& param : work_flow.configObject.params) {
            LOG_INFO("PARAM:{}:{}", param.key, param.value);
        }

        if (IsValidLogicType(work_flow.configObject.condition.type)) {
            LOG_INFO("CONDITION:{} {} {}", work_flow.configObject.condition.keyL,
                     cosmo::LogicString(work_flow.configObject.condition.type),
                     work_flow.configObject.condition.keyR);
            for (auto& condition : work_flow.configObject.condition.list) {
                LOG_INFO("CONDITION:{} {} {}", condition.keyL, cosmo::LogicString(condition.type),
                         condition.keyR);
            }
        }
    }
    LOG_INFO("{}", "=========================================================");
}

int TaskServiceImpl::GetAlgorithmCount(const std::string& algorithmId) {
    int algorithm_count = 0;
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (const auto& kv : tasks_) {
        const auto& task = kv.second;
        if (task) {
            if (task->GetAlgId() == algorithmId) {
                algorithm_count++;
            }
        }
    }
    return algorithm_count;
}

size_t TaskServiceImpl::TaskCount() {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return tasks_.size();
}

void TaskServiceImpl::AddTaskChannel(const cosmo::MsgTaskCreateRecv& taskCreateRecv) {
    task_base_->AddTaskChannel(taskCreateRecv);
}

void TaskServiceImpl::DeleteTaskChannel(const cosmo::MsgTaskCancleRecv& task_cancel_recv) {
    std::string channel_id   = GetTaskChannel(task_cancel_recv.taskId);
    std::string algorithm_id = GetTaskAlgId(task_cancel_recv.taskId);
    task_base_->DeleteTaskChannel(task_cancel_recv, channel_id, algorithm_id);
}

void TaskServiceImpl::GetCameraInfo(std::vector<cosmo::MsgCameraInfo>& cameraInfos) {
    task_base_->GetCameraInfo(cameraInfos);
}

std::vector<std::pair<std::string, cosmo::util::DurationStatInfo>> TaskServiceImpl::GetTaskActionDurations(
    const std::string& taskId, int duration_ms) {
    static const std::map<std::string_view, std::string_view> name_map = {
        {cosmo::AADetect_Code, "Detect"},
        {cosmo::AATrack_Code, "Track"},
        {cosmo::AAClassify_Code, "Classify"},
        {cosmo::AALandmark_Code, "Landmark"},
        {cosmo::AARecognizer_Code, "Recognize"},
        {cosmo::AAPersonFace_Code, "PersonFace"},
        {cosmo::AAFilter_Code, "Filter"},
        {cosmo::AACluster_Code, "Cluster"},
        {cosmo::AAFightClassify_Code, "FightClassify"},
        {cosmo::AAVideoDiagnosis_Code, "VideoDiag"},
        {cosmo::AAOcr_Code, "OCR"},
        {cosmo::AAIrCheck_Code, "IrCheck"},
        {cosmo::DADinoDetect_Code, "DinoDet"},
        {cosmo::DASam2Segment_Code, "Sam2Seg"},
        {cosmo::DAQwen3VL_Code, "Qwen3VL"},
        {cosmo::AAClassifyGroup_Code, "GroupClassify"},
        {cosmo::AAClassifyArea_Code, "AreaClassify"},
        {cosmo::AAClassifyAttr_Code, "AttrClassify"},
        {cosmo::AAClassifyMultPic_Code, "CamMoveClassify"},
        {cosmo::BAFilter_Code, "BizFilter"},
        {cosmo::BALogicalJudgment_Code, "Logic"},
        {cosmo::BAActionBranch_Code, "Branch"},
        {cosmo::BASensitivity_Code, "Sensitivity"},
        {cosmo::BAFixCountSensitivity_Code, "FixSensitivity"},
        {cosmo::BAPositiveSaveSensitivity_Code, "PosSaveSensitivity"},
        {cosmo::BATaskAlarm_Code, "Alarm"},
        {cosmo::BATaskFaceAlarm_Code, "FaceAlarm"},
        {cosmo::BATaskCollect_Code, "Collect"},
        {cosmo::BAAreaAlarm_Code, "AreaAlarm"},
        {cosmo::BAFaceLogic_Code, "FaceLogic"},
        {cosmo::BAFriendDistance_Code, "FriendDist"},
        {cosmo::BAFilterLogic_Code, "FilterLogic"},
        {cosmo::BAAssoTarget_Code, "AssoTarget"},
        {cosmo::BATargetChooseBest_Code, "ChooseBest"},
        {cosmo::GADetectTrack_Code, "DetTrack"},
    };

    std::vector<std::pair<std::string, cosmo::util::DurationStatInfo>> result;
    std::shared_lock<std::shared_mutex> lock(mtx_);
    auto it = tasks_.find(taskId);
    if (it == tasks_.end() || !it->second || !it->second->is_started) {
        return result;
    }

    for (auto& action : it->second->actions) {
        if (action.actionInst) {
            // AlgChannel special case: return decode duration
            auto channel_inst = std::dynamic_pointer_cast<cosmo::AlgChannel>(action.actionInst);
            if (channel_inst) {
                auto channel_durations = channel_inst->GetChannelDurations(duration_ms);
                result.insert(result.end(), channel_durations.begin(), channel_durations.end());
            } else {
                auto dur     = action.actionInst->GetDurationInfo(duration_ms);
                auto name_it = name_map.find(action.action.actionId);
                std::string name =
                    (name_it != name_map.end()) ? std::string(name_it->second) : action.action.actionName;
                result.emplace_back(name, dur);
            }
        }
    }
    return result;
}

}  // namespace cosmo::service
