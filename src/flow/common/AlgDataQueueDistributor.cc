// AlgDataQueueDistributor — Alg Data Queue Distributor implementation.

#include "flow/common/AlgDataQueueDistributor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <unordered_map>

#include "flow/common/ExperimentalDeadlineSelector.h"
#include "flow/detect/BenchCrossGroupPolicy.h"
#include "flow/detect/BenchDetectorPlacement.h"
#include "flow/detect/BenchSharedPair.h"
#include "util/VideoInfo.h"
#include "util/dto/ActionCodes.h"
#ifdef COSMO_NN_USE_AXERA_BACKEND
#include "flow/common/NativeFacePairPolicy.h"
#include "flow/detect/NativeFaceRoute.h"
#endif

namespace cosmo {
namespace {
    bool AllBenchOutputGroups(const std::vector<AlgDataTask>& groups) {
        return AllBenchOutputGroupsPolicy(groups);
    }
    bool CurrentBenchPair(const std::string& channel, const std::vector<AlgDataTask>& groups) {
        return CurrentBenchPairPolicy(channel, groups, BenchmarkCrossChannelEnabled());
    }
    bool CurrentSingleBenchOutput(const std::string& channel, const std::string& task,
                                  const std::shared_ptr<AlgDataQueue<AlgDataPtr>>& queue,
                                  const std::vector<AlgDataTask>& groups) {
        return CurrentSingleBenchOutputPolicy(channel, task, queue, groups, BenchmarkCrossChannelEnabled());
    }
}  // namespace

AlgDataQueueDistributor::AlgDataQueueDistributor(const std::string& name) : name_(name) {
    LOG_INFO("[{}] Init", name_);
}

AlgDataQueueDistributor::~AlgDataQueueDistributor() {
    registration_authority_->Set(0);
    if (!tasks_.empty()) {
        LOG_ERRO("[{}] Wait For Destroy, But It Still Have SubTask.Task:{}", name_, tasks_.size());
    }
    LOG_INFO("[{}] Destroy", name_);
}

void AlgDataQueueDistributor::AdvanceRegistrationGeneration() {
    if (registration_generation_)
        registration_generation_ = registration_generation_ == std::numeric_limits<std::uint64_t>::max()
                                       ? 0
                                       : registration_generation_ + 1;
    registration_authority_->generation_ = registration_generation_;
}

bool AlgDataQueueDistributor::UpdateSubTask(AlgDataTask& taskGroup, AlgTaskUnit& newTask) {
    auto it = std::find_if(taskGroup.tasks.begin(), taskGroup.tasks.end(), [&](const auto& task) {
        return (newTask.channel_id == task.channel_id) && (newTask.task_id == task.task_id) &&
               (newTask.flowActionId == task.flowActionId);
    });
    if (it != taskGroup.tasks.end()) {
        bool changed = it->fps != newTask.fps || it->que != newTask.que || it->actionId != newTask.actionId ||
                       it->dataType != newTask.dataType;
#ifdef COSMO_NN_USE_AXERA_BACKEND
        changed = changed || it->native_face_registration != newTask.native_face_registration;
        it->native_face_registration = newTask.native_face_registration;
#endif
        // Update fps
        it->fps      = newTask.fps;
        it->actionId = newTask.actionId;
        it->dataType = newTask.dataType;
        // Update the sub-task's queue pointer to prevent holding the old queue and causing a memory leak
        if (newTask.que && it->que != newTask.que) {
            it->que = newTask.que;
        }
        UpdateTaskMaxFps(taskGroup);
        LOG_INFO("[{}] Modify Queue:{}/{} Action:{} FlowId:{} Update Fps:{}, QueName:{}", name_,
                 newTask.channel_id, newTask.task_id, newTask.actionId, newTask.flowActionId, newTask.fps,
                 newTask.que->Name());
        return changed;
    }

    taskGroup.tasks.push_back(newTask);
    UpdateTaskMaxFps(taskGroup);
    LOG_INFO("[{}] Add Queue:{}/{} Action:{} FlowId:{} Update Fps:{}, QueName:{}", name_, newTask.channel_id,
             newTask.task_id, newTask.actionId, newTask.flowActionId, newTask.fps, newTask.que->Name());
    return true;
}

bool AlgDataQueueDistributor::OutQueueExist(AlgTaskUnit& newTask, bool& changed) {
    for (auto& taskGroup : tasks_) {
        if (!taskGroup.que) {
            continue;
        }

        if (taskGroup.que->Name() == newTask.que->Name()) {
            // After a task is switched off and on, AlgActionBase::Start() will recreate the data_queue.
            // At this point, newTask.que points to the new queue, but taskGroup.que still points to the old
            // stopped queue. We must update taskGroup.que, otherwise the shared_ptr of the old queue will be
            // held forever, causing a memory leak, and the new queue will not receive any data.
            if (taskGroup.que != newTask.que) {
                LOG_INFO("[{}] Queue '{}' pointer updated (old stopped queue released)", name_,
                         newTask.que->Name());
                taskGroup.que = newTask.que;
                changed       = true;
            }
            changed = UpdateSubTask(taskGroup, newTask) || changed;
            return true;
        }
    }

    return false;
}

bool AlgDataQueueDistributor::RegistProcQueue(AlgTaskUnit newTask) {
    if (!newTask.que) {
        LOG_WARN("[{}] Add Queue:{}/{} Action:{} FlowId:{} Fps:{} But The Queue Is Empty", name_,
                 newTask.channel_id, newTask.task_id, newTask.actionId, newTask.flowActionId, newTask.fps);
        return false;
    }
    // When less than 0, do full frame detection, set a very large frame rate
    if (newTask.fps < 0) {
        newTask.fps = media::kFpsCtrlMaxFps;
    }

    std::lock_guard<std::shared_mutex> lock(task_mtx_);
    std::unique_lock<std::shared_mutex> authority_lock(registration_authority_->mutex_);
    bool changed = false;
    if (OutQueueExist(newTask, changed)) {
        if (changed) {
            AdvanceRegistrationGeneration();
            ++sign_;
            UpdateMaxFps();
        }
        return true;
    }

    AlgDataTask taskGroup;
    taskGroup.group_id     = util::GenerateUUID();
    taskGroup.que          = newTask.que;
    taskGroup.max_task_fps = newTask.fps;
    if (newTask.fps > max_fps_) {
        max_fps_ = newTask.fps;
    }
    sign_ += 1;
    AdvanceRegistrationGeneration();
    taskGroup.tasks.push_back(newTask);
    tasks_.push_back(taskGroup);
    LOG_INFO("[{}] groupId:{} Add Queue:{}/{} Action:{} FlowId:{} Fps:{}, QueName:{} SubTaskSize:{}", name_,
             taskGroup.group_id, newTask.channel_id, newTask.task_id, newTask.actionId, newTask.flowActionId,
             newTask.fps, newTask.que->Name(), tasks_.size());
    return true;
}

bool AlgDataQueueDistributor::RemoveProcQueue(AlgTaskUnit newTask) {
    bool bNeedRmvGroup = false;
    std::string groupId;
    std::lock_guard<std::shared_mutex> lock(task_mtx_);
    std::unique_lock<std::shared_mutex> authority_lock(registration_authority_->mutex_);
    for (auto& taskGroup : tasks_) {
        auto it = std::find_if(taskGroup.tasks.begin(), taskGroup.tasks.end(), [&](const AlgTaskUnit& task) {
            return ((task.channel_id == newTask.channel_id) && (task.task_id == newTask.task_id) &&
                    (task.actionId == newTask.actionId) && (task.flowActionId == newTask.flowActionId));
        });
        if (it != taskGroup.tasks.end()) {
            sign_ += 1;
            AdvanceRegistrationGeneration();
            taskGroup.tasks.erase(it);
            UpdateMaxFps();
            LOG_INFO("{} groupId:{} Remove Queue:{}/{} Action:{} FlowId:{} Success", name_,
                     taskGroup.group_id, newTask.channel_id, newTask.task_id, newTask.actionId,
                     newTask.flowActionId);
            if (taskGroup.tasks.empty()) {
                bNeedRmvGroup = true;
                groupId       = taskGroup.group_id;
                break;
            }
            return true;
        }
    }

    if (bNeedRmvGroup) {
        auto it = std::find_if(tasks_.begin(), tasks_.end(),
                               [&](const AlgDataTask& task) { return task.group_id == groupId; });
        if (it != tasks_.end()) {
            sign_ += 1;
            tasks_.erase(it);
            LOG_INFO("{} Remove Queue Group {} Success", name_, groupId);
            return true;
        }
    }

    // Exact four-field match failed; try fallback match by channelId + taskId only.
    // This handles cases where flowActionId was regenerated (e.g., task refresh in TaskStart).
    for (auto itGroup = tasks_.begin(); itGroup != tasks_.end(); ++itGroup) {
        auto& taskGroup = *itGroup;
        auto it = std::find_if(taskGroup.tasks.begin(), taskGroup.tasks.end(), [&](const AlgTaskUnit& task) {
            return ((task.channel_id == newTask.channel_id) && (task.task_id == newTask.task_id));
        });
        if (it != taskGroup.tasks.end()) {
            sign_ += 1;
            AdvanceRegistrationGeneration();
            LOG_WARN(
                "{} groupId:{} Remove Queue:{}/{} Action:{} FlowId:{} via FALLBACK match "
                "(original FlowId:{})",
                name_, taskGroup.group_id, newTask.channel_id, newTask.task_id, it->actionId,
                it->flowActionId, newTask.flowActionId);
            taskGroup.tasks.erase(it);
            UpdateMaxFps();
            if (taskGroup.tasks.empty()) {
                LOG_INFO("{} Remove Queue Group {} via fallback", name_, taskGroup.group_id);
                tasks_.erase(itGroup);
            }
            return true;
        }
    }

    LOG_WARN("{} Remove Queue:{}/{} Action:{} FlowId:{} Failed", name_, newTask.channel_id, newTask.task_id,
             newTask.actionId, newTask.flowActionId);
    return false;
}

// float AlgDataQueueDistributor::GetTaskMaxFps() {
//     float fps = 0.0;
//     std::shared_lock<std::shared_mutex> lock(task_mtx_);
//     for (auto& taskGroup : tasks_) {
//         for (auto& task : taskGroup.tasks) {
//             if (task.fps > fps) {
//                 fps = task.fps;
//             }
//         }
//     }

// }

float AlgDataQueueDistributor::GetMaxFps() {
    std::shared_lock<std::shared_mutex> lock(task_mtx_);
    return max_fps_;
}

int AlgDataQueueDistributor::DistributorData(AlgDataPtr Data) {
    in_fps_ = input_fps_calc_.Fps();
    data_index_++;
    UpdateCtrlFps();
    if (!Data) {
        return 0;
    }
    int msgCount = 0;
    std::shared_lock<std::shared_mutex> lock(task_mtx_);
    for (auto& taskGroup : tasks_) {
        if (out_fps_ctl_.IsFilter(data_index_, taskGroup.max_task_fps,
                                  Data->firstTimePoint)) {  // Frame rate filtering
            continue;
        }

        if (taskGroup.que) {
            msgCount += 1;
            taskGroup.que->Insert(Data);
        }
    }

    return msgCount;
}

#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
bool AlgDataQueueDistributor::SnapshotHelmetHop(const std::string& channel, const std::string& task,
                                                const std::shared_ptr<AlgDataQueue<AlgDataPtr>>& expected,
                                                AlgDataQueue<AlgDataPtr>::LifecycleSnapshot& snapshot) {
    return SnapshotHelmetHop(channel, task, expected, snapshot, false);
}
bool AlgDataQueueDistributor::SnapshotHelmetHop(const std::string& channel, const std::string& task,
                                                const std::shared_ptr<AlgDataQueue<AlgDataPtr>>& expected,
                                                AlgDataQueue<AlgDataPtr>::LifecycleSnapshot& snapshot,
                                                bool allow_benchmark_pair) {
    std::shared_lock<std::shared_mutex> lock(task_mtx_);
    if (channel.empty() || task.empty() || !expected)
        return false;
    const bool exclusive = tasks_.size() == 1 && tasks_[0].que == expected && tasks_[0].tasks.size() == 1 &&
                           tasks_[0].tasks[0].channel_id == channel && tasks_[0].tasks[0].task_id == task;
    const bool pair = allow_benchmark_pair && CurrentBenchPair(channel, tasks_) &&
                      std::any_of(tasks_.begin(), tasks_.end(), [&](const auto& group) {
                          return group.que == expected && group.tasks[0].task_id == task;
                      });
    const bool cross_single =
        allow_benchmark_pair && CurrentSingleBenchOutput(channel, task, expected, tasks_);
    if (!exclusive && !pair && !cross_single)
        return false;
    snapshot = expected->Lifecycle();
    return snapshot.running && snapshot.generation;
}
bool AlgDataQueueDistributor::DistributorHelmetData(AlgDataPtr data) {
    if (!data || !data->native_helmet_ticket || data->channelId != data->native_helmet_ticket->Channel())
        return false;
    in_fps_ = input_fps_calc_.Fps();
    ++data_index_;
    UpdateCtrlFps();
    std::shared_lock<std::shared_mutex> lock(task_mtx_);
    // The candidate is exclusive and has exactly one task and one queue.
    if (tasks_.size() != 1 || !tasks_[0].que || tasks_[0].tasks.size() != 1)
        return false;
    auto& group      = tasks_[0];
    const auto& task = group.tasks[0];
    if (task.channel_id != data->channelId ||
        out_fps_ctl_.IsFilter(data_index_, group.max_task_fps, data->firstTimePoint))
        return false;
    const auto lifecycle = group.que->Lifecycle();
    const auto identity  = std::static_pointer_cast<const void>(group.que);
    if (!lifecycle.running ||
        !data->native_helmet_ticket->Forward(identity, lifecycle.generation, task.task_id))
        return false;
    // InsertIfLifecycle owns the queue lock and returns the actual enqueue
    // outcome. On rejection this wrapper and its ticket fall out of scope.
    return group.que->InsertIfLifecycle(std::move(data), lifecycle);
}
#endif

int AlgDataQueueDistributor::DistributorData(AlgDataPtr Frame, VideoFramePtr Data,
                                             std::function<AlgDataPtr(AlgDataPtr, VideoFramePtr)> func) {
    in_fps_ = input_fps_calc_.Fps();
    data_index_++;
    UpdateCtrlFps();
    if (!Data) {
        return 0;
    }
    int msgCount      = 0;
    AlgDataPtr inData = nullptr;
    std::shared_lock<std::shared_mutex> lock(task_mtx_);
    for (auto& taskGroup : tasks_) {
        if (out_fps_ctl_.IsFilter(data_index_, taskGroup.max_task_fps,
                                  Frame->firstTimePoint)) {  // Frame rate filtering
            continue;
        }

        if (taskGroup.que) {
            if (nullptr == inData) {
                inData = func(Frame, Data);
            }
            msgCount += 1;
            taskGroup.que->Insert(inData);
        }
    }

    return msgCount;
}

AlgFrameDistributionPlan AlgDataQueueDistributor::PrepareFrameDistribution(AlgDataPtr frame,
                                                                           std::uint64_t stream_generation) {
    AlgFrameDistributionPlan plan;
    in_fps_ = input_fps_calc_.Fps();
    data_index_++;
    UpdateCtrlFps();
    if (!frame) {
        return plan;
    }

    std::shared_lock<std::shared_mutex> lock(task_mtx_);
    plan.distributor_identity    = distributor_identity_;
    plan.registration_generation = registration_generation_;
    plan.registration_authority  = registration_authority_;
#ifdef COSMO_NN_USE_AXERA_BACKEND
    bool face_eligible = true;
#endif
    for (auto& task_group : tasks_) {
        if (task_group.que)
            plan.registered_queues.push_back({task_group.que, task_group.que->Lifecycle()});
        const bool metadata_only_group =
            !task_group.tasks.empty() &&
            std::all_of(task_group.tasks.begin(), task_group.tasks.end(),
                        [](const AlgTaskUnit& task) { return task.actionId == BAStreamChannel_Code.data(); });
        if (metadata_only_group) {
            // The camera-owned BA_00001 ChannelTask inherits an empty
            // AlgActionBase::HandFrame. It keeps channel lifecycle/FPS status
            // alive but never reads decoded pixels, so forwarding the original
            // packet metadata preserves that behavior without forcing an MPP
            // frame to be copied to Host I420.
            if (task_group.que && task_group.que->CanAccept()) {
                task_group.que->Insert(frame);
            } else if (task_group.que) {
                task_group.que->RecordDiscard();
            }
            continue;
        }
        // Check every registered consumer, including queues skipped by FPS or
        // saturation. A sampled subset cannot authorize a native-only route.
        const bool detector_group =
            !task_group.tasks.empty() &&
            std::all_of(task_group.tasks.begin(), task_group.tasks.end(),
                        [](const AlgTaskUnit& task) { return task.actionId == AADetect_Code.data(); });
        plan.native_inference_eligible = plan.native_inference_eligible && detector_group;
#ifdef COSMO_NN_USE_AXERA_BACKEND
        std::shared_ptr<const NativeFaceRegistration> group_registration;
        for (const auto& task : task_group.tasks) {
            if (!task.native_face_registration ||
                (group_registration && group_registration != task.native_face_registration))
                face_eligible = false;
            else
                group_registration = task.native_face_registration;
        }
#endif
        if (!task_group.que || !task_group.que->Lifecycle().running)
            plan.native_discardable = false;
        bool skip_frame   = false;
        bool latest_frame = false;
#ifdef COSMO_NN_USE_AXERA_BACKEND
        latest_frame = group_registration && group_registration->e24_;
#endif
        const float task_fps = task_group.max_task_fps;
        if (latest_frame && task_group.que && std::isfinite(task_fps) && task_fps > 0.0f &&
            task_fps < 1000.0f) {
            static DeadlineSelector<AlgDataQueue<AlgDataPtr>> selector;
            const auto presentation_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(frame->firstTimePoint.time_since_epoch())
                    .count();
            const auto period_ns = static_cast<std::int64_t>(std::llround(1000000000.0 / task_fps));
            skip_frame = !selector.Allow(task_group.group_id, task_group.que, presentation_ns, period_ns,
                                         stream_generation);
        } else {
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
            const char* deadline_opt = std::getenv("COSMO_AXERA_EXPERIMENTAL_DEADLINE_FPS");
            const bool bench_group =
                BenchmarkCrossChannelEnabled() && detector_group && !task_group.tasks.empty() &&
                std::all_of(task_group.tasks.begin(), task_group.tasks.end(), [](const AlgTaskUnit& task) {
                    return IsScenarioBenchDetectorTask("9275710", task.channel_id, task.task_id);
                });
            if (deadline_opt && std::strcmp(deadline_opt, "1") == 0 && bench_group && task_group.que &&
                std::isfinite(task_fps) && task_fps > 0.0f && task_fps < 1000.0f) {
                static DeadlineSelector<AlgDataQueue<AlgDataPtr>> selector;
                const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count();
                const auto period = static_cast<std::int64_t>(std::llround(1000000000.0 / task_fps));
                skip_frame        = !selector.Allow(task_group.group_id, task_group.que, now, period);
            } else
#endif
                skip_frame = out_fps_ctl_.IsFilter(data_index_, task_fps, frame->firstTimePoint);
        }
        if (skip_frame)
            continue;
        if (!task_group.que) {
            continue;
        }
        if (!latest_frame && !task_group.que->CanAccept()) {
            task_group.que->RecordDiscard();
            continue;
        }
        plan.queues.push_back(task_group.que);
#ifdef COSMO_NN_USE_AXERA_BACKEND
        plan.native_destinations.push_back({task_group.que, std::move(group_registration)});
#endif
    }
#ifdef COSMO_NN_USE_AXERA_BACKEND
    if (!face_eligible || !plan.native_inference_eligible)
        plan.native_destinations.clear();
    if (plan.native_destinations.size() == 1)
        plan.native_face_registration = plan.native_destinations[0].registration;
#endif
    return plan;
}

bool AlgDataQueueDistributor::IsCurrentPlanLocked(const AlgFrameDistributionPlan& plan) const {
    if (!plan.registration_generation || plan.registration_generation != registration_generation_ ||
        plan.distributor_identity != distributor_identity_ ||
        plan.registration_authority != registration_authority_)
        return false;
    std::size_t index = 0;
    for (const auto& group : tasks_) {
        if (!group.que)
            continue;
        if (index >= plan.registered_queues.size())
            return false;
        const auto& recorded = plan.registered_queues[index++];
        const auto lifecycle = group.que->Lifecycle();
        if (recorded.queue != group.que || !lifecycle.generation ||
            recorded.lifecycle.generation != lifecycle.generation ||
            recorded.lifecycle.running != lifecycle.running)
            return false;
    }
    if (index != plan.registered_queues.size())
        return false;
    for (const auto& selected : plan.queues) {
        if (!selected || std::none_of(plan.registered_queues.begin(), plan.registered_queues.end(),
                                      [&](const auto& recorded) { return recorded.queue == selected; }))
            return false;
    }
    return true;
}

bool AlgDataQueueDistributor::IsCurrentPlan(const AlgFrameDistributionPlan& plan) {
    std::shared_lock<std::shared_mutex> lock(task_mtx_);
    return IsCurrentPlanLocked(plan);
}

AlgFrameDistributionPlan AlgDataQueueDistributor::SnapshotNativeOutput(const std::string& channel_id) {
    return SnapshotNativeOutput(channel_id, false);
}
AlgFrameDistributionPlan AlgDataQueueDistributor::SnapshotNativeOutput(const std::string& channel_id,
                                                                       bool allow_benchmark_pair) {
    AlgFrameDistributionPlan snapshot;
    std::shared_lock<std::shared_mutex> lock(task_mtx_);
    snapshot.distributor_identity    = distributor_identity_;
    snapshot.registration_generation = registration_generation_;
    snapshot.registration_authority  = registration_authority_;
    std::size_t task_count           = 0;
    const bool cross = allow_benchmark_pair && BenchmarkCrossChannelEnabled() && AllBenchOutputGroups(tasks_);
    for (const auto& group : tasks_) {
        for (const auto& task : group.tasks) {
            if (task.channel_id == channel_id)
                ++task_count;
            else if (!cross)
                snapshot.native_inference_eligible = false;
        }
        if (group.que) {
            snapshot.registered_queues.push_back({group.que, group.que->Lifecycle()});
        } else {
            snapshot.native_inference_eligible = false;
        }
    }
    if (channel_id.empty() ||
        (task_count != 1 && !(allow_benchmark_pair && CurrentBenchPair(channel_id, tasks_))))
        snapshot.native_inference_eligible = false;
    return snapshot;
}

#ifdef COSMO_NN_USE_AXERA_BACKEND
bool AlgDataQueueDistributor::HasE24NativeCapture() {
    std::shared_lock<std::shared_mutex> lock(task_mtx_);
    for (const auto& group : tasks_) {
        for (const auto& task : group.tasks) {
            if (task.native_face_registration && task.native_face_registration->e24_)
                return true;
        }
    }
    return false;
}
#endif

std::unique_ptr<AlgDataQueueDistributor::NativeOutputTransaction>
AlgDataQueueDistributor::AcquireNativeOutput(const AlgFrameDistributionPlan& snapshot) {
    std::shared_lock<std::shared_mutex> lock(task_mtx_);
    if (!IsCurrentPlanLocked(snapshot) || !snapshot.native_inference_eligible ||
        snapshot.registered_queues.empty())
        return nullptr;
    return std::unique_ptr<NativeOutputTransaction>(
        new NativeOutputTransaction(this, std::move(lock), snapshot));
}

int AlgDataQueueDistributor::DistributorNativeOutput(
    NativeOutputTransaction& transaction, const std::string& channel_id, AlgDataPtr data,
    std::function<AlgDataPtr(AlgDataPtr, std::string)> func) {
    if (transaction.owner_ != this || !transaction.lock_.owns_lock() || !data || !func ||
        !IsCurrentPlanLocked(transaction.snapshot_))
        return 0;
    in_fps_ = input_fps_calc_.Fps();
    ++data_index_;
    UpdateCtrlFps();
    int count         = 0;
    std::size_t index = 0;
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
    const bool benchmark_pair = CurrentBenchPair(channel_id, tasks_);
    const bool cross_bench    = BenchmarkCrossChannelEnabled() && AllBenchOutputGroups(tasks_);
    struct BenchFpsState {
        std::weak_ptr<const void> owner;
        std::uint64_t generation{0};
        std::unordered_map<std::string, util::FpsCtrl> controls;
        std::unordered_map<std::string, util::FpsCalc> input_fps;
        std::unordered_map<std::string, std::size_t> input_index;
    };
    // This detector's output is produced by its sole worker. The weak identity
    // prevents pointer reuse; state is reclaimed when that worker exits.
    static thread_local BenchFpsState bench_fps;
    if ((benchmark_pair || cross_bench) && (bench_fps.owner.lock() != distributor_identity_ ||
                                            bench_fps.generation != registration_generation_)) {
        bench_fps.controls.clear();
        bench_fps.input_fps.clear();
        bench_fps.input_index.clear();
        bench_fps.owner      = distributor_identity_;
        bench_fps.generation = registration_generation_;
    }
    float channel_fps         = 0;
    std::size_t channel_index = 0;
    if (cross_bench) {
        channel_fps   = bench_fps.input_fps[channel_id].Fps();
        channel_index = ++bench_fps.input_index[channel_id];
    }
#endif
    for (const auto& group : tasks_) {
        if (!group.que)
            continue;
        const auto expected = transaction.snapshot_.registered_queues[index++].lifecycle;
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
        if (cross_bench && group.tasks.front().channel_id != channel_id)
            continue;
#endif
        bool skip = false;
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
        if (benchmark_pair || cross_bench) {
            auto& ctrl = bench_fps.controls[channel_id + "/" + group.group_id];
            ctrl.SetRealFps(cross_bench ? channel_fps : in_fps_);
            skip = ctrl.IsFilter(cross_bench ? channel_index : data_index_, group.max_task_fps,
                                 data->firstTimePoint);
        } else
#endif
            skip = out_fps_ctl_.IsFilter(data_index_, group.max_task_fps, data->firstTimePoint);
        if (skip)
            continue;
        for (const auto& task : group.tasks) {
            if (task.channel_id != channel_id)
                continue;
            auto output = func(data, task.task_id);
#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
            if (output && output->native_helmet_ticket) {
                // A ticket must be attached only to the unaliased private
                // task wrapper and exactly one registered first-hop queue.
                if ((transaction.snapshot_.registered_queues.size() != 1 &&
                     !(transaction.snapshot_.registered_queues.size() == 2 &&
                       CurrentBenchPair(channel_id, tasks_))
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
                     && !(cross_bench && (benchmark_pair || CurrentSingleBenchOutput(channel_id, task.task_id,
                                                                                     group.que, tasks_)))
#endif
                         ) ||
                    group.tasks.size() != 1 || output.use_count() != 1 || output.get() == data.get() ||
                    output->channelId != output->native_helmet_ticket->Channel() ||
                    !output->native_helmet_ticket->Forward(std::static_pointer_cast<const void>(group.que),
                                                           expected.generation, task.task_id))
                    return 0;
            }
#endif
            if (output && group.que->InsertIfLifecycle(std::move(output), expected))
                ++count;
        }
    }
    return count;
}

int AlgDataQueueDistributor::DistributorPreparedFrame(
    const AlgFrameDistributionPlan& plan, AlgDataPtr frame, VideoFramePtr data,
    std::function<AlgDataPtr(AlgDataPtr, VideoFramePtr)> func) {
    if (!frame || !data || plan.Empty()) {
        return 0;
    }

    auto converted = func(frame, data);
    if (!converted) {
        return 0;
    }

    int message_count = 0;
    for (const auto& queue : plan.queues) {
        // Give parallel detector queues their own AlgData wrapper. The native
        // owner is shared and remains valid through downstream classifier
        // preprocessing, while per-branch task state stays isolated.
        auto queued = converted;
        if (plan.queues.size() > 1 && converted->chanDataDec.native_buffer) {
            queued = AlgDataCopy(converted);
        }
        if (queue && queue->Insert(std::move(queued))) {
            message_count += 1;
        }
    }
    return message_count;
}

int AlgDataQueueDistributor::DistributorPreparedAxeraNativeFrame(const AlgFrameDistributionPlan& plan,
                                                                 AlgDataPtr frame,
                                                                 media::NativeVideoBufferPtr native,
                                                                 int64_t timestamp, int64_t stream_index) {
    return TryDistributorPreparedAxeraNativeFrame(plan, std::move(frame), std::move(native), timestamp,
                                                  stream_index) == AxeraNativeDelivery::Delivered
               ? 1
               : 0;
}

AxeraNativeDelivery AlgDataQueueDistributor::TryDistributorPreparedAxeraNativeFrame(
    const AlgFrameDistributionPlan& plan, AlgDataPtr frame, media::NativeVideoBufferPtr native,
    int64_t timestamp, int64_t stream_index) {
    std::shared_lock<std::shared_mutex> lock(task_mtx_);
    if (!IsCurrentPlanLocked(plan) || !frame || !native || !native->Valid() ||
        !plan.SupportsNativeInference() || plan.queues.size() != 1 ||
        native->memory_kind != media::NativeVideoMemoryKind::AxeraPhysical ||
        native->format != media::NativeVideoBufferFormat::NV12 || native->crop_x || native->crop_y ||
        static_cast<int64_t>(native->width) * 9 != static_cast<int64_t>(native->height) * 16)
        return AxeraNativeDelivery::Rejected;

    // Metadata consumers may retain this frame after inference completes. The
    // queue's native_buffer owns the lease; the empty provider must not extend
    // it or create host pixels when a consumer asks for them.
    auto metadata_frame = std::make_shared<media::VideoFrame>(
        native->width, native->height, media::PixelFormat::PIXEL_NV12, native->frame_index, timestamp,
        []() -> VideoFramePtr { return nullptr; });
    if (!metadata_frame->Active())
        return AxeraNativeDelivery::Rejected;
    metadata_frame->SetStreamIndex(stream_index);
    auto data                       = std::make_shared<AlgData>();
    data->chanDataOrig.packet       = frame->chanDataOrig.packet;
    data->chanDataOrig.fps          = frame->chanDataOrig.fps;
    data->dataType                  = AlgDataType::ChannelDataDec;
    data->chanDataDec.frame         = std::move(metadata_frame);
    data->chanDataDec.native_buffer = std::move(native);
    data->channelId                 = frame->channelId;
    data->firstTimePoint            = frame->firstTimePoint;
    auto& queue                     = plan.queues[0];
    if (queue->Insert(std::move(data)))
        return AxeraNativeDelivery::Delivered;
    // Insert can fail because the queue filled after preparation. A stopped
    // or restarted queue is a stale plan, never an ordinary sampled discard.
    return IsCurrentPlanLocked(plan) && queue->Lifecycle().running ? AxeraNativeDelivery::Discarded
                                                                   : AxeraNativeDelivery::Rejected;
}

#ifdef COSMO_NN_USE_AXERA_BACKEND
AxeraNativeDelivery AlgDataQueueDistributor::TryDistributorPreparedAxeraFaceFrame(
    const AlgFrameDistributionPlan& plan, AlgDataPtr frame, media::NativeVideoBufferPtr native,
    int64_t timestamp, int64_t stream_index, std::uint64_t stream_generation) {
    std::shared_lock<std::shared_mutex> lock(task_mtx_);
    if (!IsCurrentPlanLocked(plan) || !plan.SupportsNativeInference() || plan.queues.size() > 2 ||
        plan.native_destinations.size() != plan.queues.size() || !plan.registration_authority || !frame ||
        frame->channelId.empty() || !native || !native->Valid() ||
        native->memory_kind != media::NativeVideoMemoryKind::AxeraPhysical ||
        native->format != media::NativeVideoBufferFormat::NV12 || native->crop_x || native->crop_y ||
        !stream_generation)
        return AxeraNativeDelivery::Rejected;
#ifndef COSMO_AXERA_FACE_TEST_SEAM
    if (!native->frame_witness || !native->frame_witness->Matches(*native) ||
        native->frame_witness->Generation() != stream_generation ||
        native->frame_witness->FrameIndex() != native->frame_index ||
        native->frame_witness->SourceTimestamp() != timestamp)
        return AxeraNativeDelivery::Rejected;
#endif
    std::vector<const AlgDataTask*> groups;
    for (const auto& group : tasks_) {
        const bool metadata_only =
            !group.tasks.empty() &&
            std::all_of(group.tasks.begin(), group.tasks.end(),
                        [](const AlgTaskUnit& task) { return task.actionId == BAStreamChannel_Code.data(); });
        if (!metadata_only)
            groups.push_back(&group);
    }
    if (groups.empty() || groups.size() > 2)
        return AxeraNativeDelivery::Rejected;
        // A shared benchmark detector appears as one group with two task bindings.
        // The two-group production pair check below does not cover this shape.
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
    if (groups.size() == 1 && !groups[0]->tasks.empty()) {
        const auto& group = *groups[0];
        const auto reg    = group.tasks.front().native_face_registration;
        if (reg && !reg->bench_entry_.expired() && reg->bench_ticket_ && group.tasks.size() > 1) {
            if (group.tasks.size() != 2 || !group.que)
                return AxeraNativeDelivery::Rejected;
            std::array<BenchPairRoute, 2> routes{};
            for (std::size_t i = 0; i < 2; ++i) {
                const auto& task = group.tasks[i];
                if (task.actionId != AADetect_Code.data() || task.native_face_registration != reg ||
                    task.que != group.que)
                    return AxeraNativeDelivery::Rejected;
                routes[i] = {task.channel_id, task.task_id, task.que.get()};
            }
            if (!IsBenchInputPair(frame->channelId, routes))
                return AxeraNativeDelivery::Rejected;
        }
    }
#endif
    // Permit only the explicit production pair and the benchmark pair.
    // Each benchmark scene owns a separate detector, registration and output.
    if (groups.size() == 2) {
        std::array<NativePairMember, 2> members{};
        std::size_t index = 0;
        for (const auto* group : groups) {
            if (!group->que || group->tasks.size() != 1)
                return AxeraNativeDelivery::Rejected;
            const auto& task = group->tasks.front();
            const auto reg   = task.native_face_registration;
            if (!reg || task.channel_id != frame->channelId || task.actionId != AADetect_Code.data())
                return AxeraNativeDelivery::Rejected;
            auto role = !reg->entry_.expired()          ? NativePairRole::Face
                        : !reg->person_entry_.expired() ? NativePairRole::Person
                                                        : NativePairRole::Other;
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
            if (auto bench = reg->bench_entry_.lock(); bench && reg->bench_ticket_) {
                if (bench->SceneId() == "930927510")
                    role = NativePairRole::BenchPerson;
                else if (bench->SceneId() == "930007463")
                    role = NativePairRole::BenchHelmet;
            }
#endif
            members[index++] = {task.channel_id, task.task_id, role};
        }
        if (!IsSupportedNativePair(frame->channelId, members) || native->width != 1920 ||
            native->height != 1080)
            return AxeraNativeDelivery::Rejected;
    }
    using Queue = AlgDataQueue<AlgDataPtr>;
    struct PendingDelivery {
        std::shared_ptr<Queue> queue;
        AlgDataPtr data;
        Queue::LifecycleSnapshot lifecycle;
        bool latest_frame;
    };
    std::vector<PendingDelivery> pending;
    for (const auto& destination : plan.native_destinations) {
        const auto& registration = destination.registration;
        if (!registration || !destination.queue ||
            std::find(plan.queues.begin(), plan.queues.end(), destination.queue) == plan.queues.end())
            return AxeraNativeDelivery::Rejected;
        const auto person_entry = registration->person_entry_.lock();
        const bool person720    = native->width == 1280 && native->height == 720 && person_entry &&
                               person_entry->AllowsPerson720Standalone();
        if (!(native->width == 1920 && native->height == 1080) && !person720)
            return AxeraNativeDelivery::Rejected;
        const auto queue  = registration->queue_.lock();
        const auto output = registration->output_distributor_.lock();
        if (!queue || queue != destination.queue || !output || output.get() == this)
            return AxeraNativeDelivery::Rejected;
        const auto group =
            std::find_if(groups.begin(), groups.end(), [&](const auto* item) { return item->que == queue; });
        if (group == groups.end() || (*group)->tasks.empty() ||
            std::any_of((*group)->tasks.begin(), (*group)->tasks.end(), [&](const auto& task) {
                return task.channel_id != frame->channelId || task.native_face_registration != registration;
            }))
            return AxeraNativeDelivery::Rejected;
        const auto lifecycle = queue->Lifecycle();
        if (!lifecycle.running || !lifecycle.generation ||
            lifecycle.generation != registration->queue_lifecycle_.generation)
            return AxeraNativeDelivery::Rejected;
        bool benchmark_pair = false;
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
        if (const auto bench = registration->bench_entry_.lock(); bench && registration->bench_ticket_)
            benchmark_pair = bench->SceneId() == "930927510" || bench->SceneId() == "930007463";
#endif
        auto snapshot = output->SnapshotNativeOutput(frame->channelId, benchmark_pair);
        if (!snapshot.native_inference_eligible ||
            std::any_of(snapshot.registered_queues.begin(), snapshot.registered_queues.end(),
                        [&](const auto& item) { return item.queue == queue; }))
            return AxeraNativeDelivery::Rejected;
        auto metadata = std::make_shared<media::VideoFrame>(
            native->width, native->height, media::PixelFormat::PIXEL_NV12, native->frame_index, timestamp,
            media::VideoFrame::MetadataOnlyTag{});
        if (!metadata->Active())
            return AxeraNativeDelivery::Rejected;
        metadata->SetStreamIndex(stream_index);
        nn::axera::FaceFrameKey key{frame->channelId, stream_generation, native->frame_index, timestamp};
        auto envelope = std::shared_ptr<const NativeFaceEnvelope>(
            new NativeFaceEnvelope(registration, plan.registration_authority, plan.registration_generation,
                                   std::move(key), stream_index, native, std::move(snapshot)));
        auto data = std::make_shared<AlgData>();
        if (frame->chanDataOrig.packet && frame->chanDataOrig.packet->index >= 0 &&
            static_cast<std::uint64_t>(frame->chanDataOrig.packet->index) == native->frame_index &&
            frame->chanDataOrig.packet->stream_idx == stream_index)
            data->chanDataOrig.packet = frame->chanDataOrig.packet;
        data->chanDataOrig.fps                 = frame->chanDataOrig.fps;
        data->dataType                         = AlgDataType::ChannelDataDec;
        data->chanDataDec.frame                = std::move(metadata);
        data->chanDataDec.native_buffer        = native;
        data->chanDataDec.native_face_envelope = std::move(envelope);
        data->channelId                        = frame->channelId;
        data->firstTimePoint                   = frame->firstTimePoint;
        pending.push_back({queue, std::move(data), lifecycle, registration->e24_});
    }
    bool delivered = false;
    for (auto& item : pending) {
        const bool inserted = item.latest_frame
                                  ? item.queue->InsertLatestIfLifecycle(std::move(item.data), item.lifecycle)
                                  : item.queue->InsertIfLifecycle(std::move(item.data), item.lifecycle);
        delivered           = inserted || delivered;
    }
    return delivered ? AxeraNativeDelivery::Delivered : AxeraNativeDelivery::Discarded;
}
#endif

// Only send to the channel registered by the task. Used for detection data distribution.
// When the detector is multiplexed to multiple channels, those of the same channel are distributed through
// channelId.
int AlgDataQueueDistributor::DistributorData(const std::string& channelId, AlgDataPtr Data,
                                             std::function<AlgDataPtr(AlgDataPtr, std::string)> func) {
    in_fps_ = input_fps_calc_.Fps();
    data_index_++;
    UpdateCtrlFps();
    if (!Data) {
        return 0;
    }
    int msgCount = 0;
    std::shared_lock<std::shared_mutex> lock(task_mtx_);
    for (auto& taskGroup : tasks_) {
        if (out_fps_ctl_.IsFilter(data_index_, taskGroup.max_task_fps,
                                  Data->firstTimePoint)) {  // Frame rate filtering
            continue;
        }

        LOG_DEBUG("{}/{} Task:{} [Distribute] groupId:{} Frames.  Data:{}", name_, channelId,
                  taskGroup.group_id, data_index_, Data->dataType);

        for (auto& task : taskGroup.tasks) {
            if (channelId == task.channel_id) {
                if (taskGroup.que) {
                    msgCount += 1;
                    auto inData = func(Data, task.task_id);
                    if (inData)
                        taskGroup.que->Insert(inData);
                }
            }
        }
    }

    return msgCount;
}

void AlgDataQueueDistributor::UpdateMaxFps() {
    float maxFps = 0.0;

    for (auto& unit : tasks_) {
        UpdateTaskMaxFps(unit);
        if (unit.max_task_fps > maxFps)
            maxFps = unit.max_task_fps;
    }
    max_fps_ = maxFps;
    return;
}

void AlgDataQueueDistributor::UpdateTaskMaxFps(AlgDataTask& taskGroup) {
    float maxFps = 0.0;
    if (!taskGroup.tasks.empty()) {
        auto it = std::max_element(taskGroup.tasks.begin(), taskGroup.tasks.end(),
                                   [](const auto& a, const auto& b) { return a.fps < b.fps; });
        maxFps  = it->fps;
    }
    taskGroup.max_task_fps = maxFps;
}

void AlgDataQueueDistributor::UpdateCtrlFps() {
    // Update once every 100 frames; can be changed to timed update in the future
    if ((data_index_ < 200) || (0 == data_index_ % 100)) {
        double infps   = in_fps_;
        double absDiff = std::abs(infps - out_fps_ctl_.GetRealFps());
        if (absDiff > 0.1) {
            out_fps_ctl_.SetRealFps(in_fps_);
        }
    }
}

std::vector<AlgDataTask> AlgDataQueueDistributor::GetBindTasks() {
    std::vector<AlgDataTask> bindTasks;
    std::shared_lock<std::shared_mutex> lock(task_mtx_);
    for (auto& taskGroup : tasks_) {
        AlgDataTask bindChannel;
        bindChannel.group_id     = taskGroup.group_id;
        bindChannel.max_task_fps = taskGroup.max_task_fps;
        bindChannel.que          = taskGroup.que;
        for (auto& task : taskGroup.tasks) {
            AlgTaskUnit bindTask;
            bindTask.channel_id   = task.channel_id;
            bindTask.task_id      = task.task_id;
            bindTask.actionId     = task.actionId;
            bindTask.flowActionId = task.flowActionId;
            bindTask.fps          = task.fps;
            bindTask.que          = task.que;
#ifdef COSMO_NN_USE_AXERA_BACKEND
            bindTask.native_face_registration = task.native_face_registration;
#endif
            bindChannel.tasks.push_back(bindTask);
        }
        bindTasks.push_back(bindChannel);
    }

    return bindTasks;
}

int AlgDataQueueDistributor::ForceRemoveByTaskId(const std::string& taskId) {
    int removedCount = 0;
    std::lock_guard<std::shared_mutex> lock(task_mtx_);
    std::unique_lock<std::shared_mutex> authority_lock(registration_authority_->mutex_);

    for (auto& taskGroup : tasks_) {
        auto it = taskGroup.tasks.begin();
        while (it != taskGroup.tasks.end()) {
            if (it->task_id == taskId) {
                LOG_INFO("{} ForceRemove Queue taskId:{} channelId:{} Action:{} FlowId:{}", name_,
                         it->task_id, it->channel_id, it->actionId, it->flowActionId);
                it = taskGroup.tasks.erase(it);
                sign_ += 1;
                AdvanceRegistrationGeneration();
                removedCount++;
            } else {
                ++it;
            }
        }
    }

    // Remove empty groups
    auto grpIt = tasks_.begin();
    while (grpIt != tasks_.end()) {
        if (grpIt->tasks.empty()) {
            LOG_INFO("{} ForceRemove empty group {}", name_, grpIt->group_id);
            grpIt = tasks_.erase(grpIt);
        } else {
            ++grpIt;
        }
    }

    if (removedCount > 0) {
        UpdateMaxFps();
        LOG_INFO("{} ForceRemoveByTaskId:{} removed {} entries, remaining groups:{}", name_, taskId,
                 removedCount, tasks_.size());
    }
    return removedCount;
}

}  // namespace cosmo
