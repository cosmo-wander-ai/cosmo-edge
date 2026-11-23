#pragma once

#include <shared_mutex>
#include <vector>

#include "flow/common/AlgDataQueue.h"
#include "flow/common/AlgDataUnit.h"
#include "util/FpsCalc.h"
#include "util/FpsCtrl.h"

namespace cosmo {
#ifdef COSMO_NN_USE_AXERA_BACKEND
class NativeFaceRegistration;
#endif
// Internal, non-serializable registration authority. A lease prevents a task
// mutation from overtaking an in-flight native inference. It is not itself
// model, color, or scenario authorization.
class RegistrationAuthority {
public:
    class Lease {
    public:
        Lease(Lease&&) noexcept            = default;
        Lease& operator=(Lease&&) noexcept = default;
        Lease(const Lease&)                = delete;
        Lease& operator=(const Lease&)     = delete;
        explicit operator bool() const noexcept {
            return valid_;
        }
        bool Matches(const RegistrationAuthority* owner, std::uint64_t generation) const noexcept {
            return valid_ && lock_.owns_lock() && owner_ == owner && generation_ == generation;
        }

    private:
        friend class RegistrationAuthority;
        Lease(std::shared_lock<std::shared_mutex> lock, bool valid, const RegistrationAuthority* owner,
              std::uint64_t generation)
            : lock_(std::move(lock)), valid_(valid), owner_(owner), generation_(generation) {}
        std::shared_lock<std::shared_mutex> lock_;
        bool valid_{false};
        const RegistrationAuthority* owner_{nullptr};
        std::uint64_t generation_{0};
    };
    Lease Acquire(std::uint64_t expected) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return Lease(std::move(lock), expected != 0 && generation_ == expected, this, expected);
    }

private:
    friend class AlgDataQueueDistributor;
    void Set(std::uint64_t value) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        generation_ = value;
    }
    mutable std::shared_mutex mutex_;
    std::uint64_t generation_{1};
};
struct AlgTaskUnit {
    std::string channel_id;    // channelId of the registrant
    std::string task_id;       // taskId of the registrant
    std::string actionId;      // ActionId of the registrant
    std::string flowActionId;  // Flow ActionId of the registrant
    float fps{-1.0};           // Frame rate of the registrant
    AlgDataType dataType{AlgDataType::ChannelDataDec};
    std::shared_ptr<AlgDataQueue<AlgDataPtr>> que;
#ifdef COSMO_NN_USE_AXERA_BACKEND
    std::shared_ptr<const NativeFaceRegistration> native_face_registration;
#endif
};
struct AlgDataTask {
    float max_task_fps{0.0};
    std::string group_id;
    std::shared_ptr<AlgDataQueue<AlgDataPtr>> que;  // Channel multiplexing. E.g., tripwire and intrusion
                                                    // obtain the same detector, but different tasks
    std::vector<AlgTaskUnit> tasks;
};

struct AlgFrameDistributionPlan {
    using Queue = AlgDataQueue<AlgDataPtr>;
    struct RegisteredQueue {
        std::shared_ptr<Queue> queue;
        Queue::LifecycleSnapshot lifecycle;
    };
    std::vector<std::shared_ptr<AlgDataQueue<AlgDataPtr>>> queues;
    std::vector<RegisteredQueue> registered_queues;
    std::shared_ptr<const void> distributor_identity;
    std::uint64_t registration_generation{0};
    std::shared_ptr<const RegistrationAuthority> registration_authority;
    bool native_inference_eligible{true};
    bool native_discardable{true};
#ifdef COSMO_NN_USE_AXERA_BACKEND
    std::shared_ptr<const NativeFaceRegistration> native_face_registration;
    struct NativeDestination {
        std::shared_ptr<Queue> queue;
        std::shared_ptr<const NativeFaceRegistration> registration;
    };
    std::vector<NativeDestination> native_destinations;
#endif

    [[nodiscard]] bool Empty() const {
        return queues.empty();
    }

    [[nodiscard]] bool SupportsNativeInference() const {
        return !queues.empty() && native_inference_eligible;
    }

    [[nodiscard]] bool CanDiscardNativeFrame() const {
        return Empty() && native_inference_eligible && native_discardable;
    }
};
enum class AxeraNativeDelivery { Delivered, Discarded, Rejected };

class AlgDataQueueDistributor {
public:
    class NativeOutputTransaction {
        friend class AlgDataQueueDistributor;
        NativeOutputTransaction(const AlgDataQueueDistributor* owner,
                                std::shared_lock<std::shared_mutex> lock, AlgFrameDistributionPlan snapshot)
            : owner_(owner), lock_(std::move(lock)), snapshot_(std::move(snapshot)) {}
        const AlgDataQueueDistributor* owner_;
        std::shared_lock<std::shared_mutex> lock_;
        AlgFrameDistributionPlan snapshot_;
    };
    explicit AlgDataQueueDistributor(const std::string& moduleName);

    ~AlgDataQueueDistributor();

    // Register task processing queue
    bool RegistProcQueue(AlgTaskUnit newTask);

    bool RemoveProcQueue(AlgTaskUnit newTask);

    // Distribute data to all registered queues
    int DistributorData(AlgDataPtr Data);
#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
    bool DistributorHelmetData(AlgDataPtr data);
    bool SnapshotHelmetHop(const std::string& channel, const std::string& task,
                           const std::shared_ptr<AlgDataQueue<AlgDataPtr>>& expected,
                           AlgDataQueue<AlgDataPtr>::LifecycleSnapshot& snapshot);
    bool SnapshotHelmetHop(const std::string& channel, const std::string& task,
                           const std::shared_ptr<AlgDataQueue<AlgDataPtr>>& expected,
                           AlgDataQueue<AlgDataPtr>::LifecycleSnapshot& snapshot, bool allow_benchmark_pair);
#endif

    // Filter and distribute data to all registered queues, calling func for conversion after filtering
    int DistributorData(AlgDataPtr Frame, VideoFramePtr Data,
                        std::function<AlgDataPtr(AlgDataPtr, VideoFramePtr)> func);

    /// Advance frame-rate control and snapshot queues that will actually
    /// accept this frame. Queue saturation is treated as an early discard so
    /// the Rockchip decoder can skip Host I420 materialization entirely.
    AlgFrameDistributionPlan PrepareFrameDistribution(AlgDataPtr frame, std::uint64_t stream_generation = 0);

    // A generation detects registration/queue lifecycle changes, but does not
    // itself grant native model or scenario authority.
    bool IsCurrentPlan(const AlgFrameDistributionPlan& plan);
    AlgFrameDistributionPlan SnapshotNativeOutput(const std::string& channel_id);
    AlgFrameDistributionPlan SnapshotNativeOutput(const std::string& channel_id, bool allow_benchmark_pair);
    std::unique_ptr<NativeOutputTransaction> AcquireNativeOutput(const AlgFrameDistributionPlan& snapshot);
    int DistributorNativeOutput(NativeOutputTransaction& transaction, const std::string& channel_id,
                                AlgDataPtr data, std::function<AlgDataPtr(AlgDataPtr, std::string)> func);

    /// Complete a previously prepared distribution after the host frame has
    /// been materialized exactly once.
    int DistributorPreparedFrame(const AlgFrameDistributionPlan& plan, AlgDataPtr frame, VideoFramePtr data,
                                 std::function<AlgDataPtr(AlgDataPtr, VideoFramePtr)> func);

    // Native-only handoff for an already admitted AX face detector. The
    // caller must bind the exact model and reject viewers/capture/other tasks
    // before calling; this function cannot infer model identity from a queue.
    // It never asks the decoder to materialize pixels or runs ColorConvert.
    int DistributorPreparedAxeraNativeFrame(const AlgFrameDistributionPlan& plan, AlgDataPtr frame,
                                            media::NativeVideoBufferPtr native, int64_t timestamp,
                                            int64_t stream_index);
    AxeraNativeDelivery TryDistributorPreparedAxeraNativeFrame(const AlgFrameDistributionPlan& plan,
                                                               AlgDataPtr frame,
                                                               media::NativeVideoBufferPtr native,
                                                               int64_t timestamp, int64_t stream_index);
#ifdef COSMO_NN_USE_AXERA_BACKEND
    bool HasE24NativeCapture();
    // Specialized handoff; requires the exact detector-issued registration.
    AxeraNativeDelivery TryDistributorPreparedAxeraFaceFrame(const AlgFrameDistributionPlan& plan,
                                                             AlgDataPtr frame,
                                                             media::NativeVideoBufferPtr native,
                                                             int64_t timestamp, int64_t stream_index,
                                                             std::uint64_t stream_generation);
#endif

    // Distribute data to specific registered queues
    // Only send to the channel registered by the task. Used for detection data distribution.
    // When the detector is multiplexed to multiple channels, those of the same channel are distributed
    // through channelId.
    int DistributorData(const std::string& channelId, AlgDataPtr Data,
                        std::function<AlgDataPtr(AlgDataPtr, std::string)> func);

    // // Get the maximum frame rate of detection tasks by polling all tasks
    // float GetTaskMaxFps();

    // Get the maximum frame rate of detection tasks (get recorded value)
    float GetMaxFps();

    void ResetDistributor() {
        std::lock_guard<std::shared_mutex> lock(task_mtx_);
        std::unique_lock<std::shared_mutex> authority_lock(registration_authority_->mutex_);
        AdvanceRegistrationGeneration();
        data_index_ = 0;
        in_fps_     = 0.0;
        input_fps_calc_.Reset();
        out_fps_ctl_.ChangeFps(0.0f, 0.0f);
    }

    std::vector<AlgDataTask> GetBindTasks();

    // Force-remove all queue entries matching taskId, regardless of flowActionId.
    // Used during task deletion to ensure no stale queue references remain.
    int ForceRemoveByTaskId(const std::string& taskId);

    int GetSign() {
        std::shared_lock<std::shared_mutex> lock(task_mtx_);
        return sign_;
    };

protected:
    inline size_t GetTaskCount() {
        std::shared_lock<std::shared_mutex> lock(task_mtx_);
        return tasks_.size();
    };

private:
    // Check if the newly registered Queue already exists in the queue
    bool OutQueueExist(AlgTaskUnit& newTask, bool& changed);

    // Update the queue
    bool UpdateSubTask(AlgDataTask& taskGroup, AlgTaskUnit& newTask);
    void AdvanceRegistrationGeneration();
    bool IsCurrentPlanLocked(const AlgFrameDistributionPlan& plan) const;

    // Update maximum frame rate when deleting a node; auto-update when adding a node if new fps is larger
    void UpdateMaxFps();

    // Update the input frame rate of the control frame rate template
    void UpdateCtrlFps();

    void UpdateTaskMaxFps(AlgDataTask& taskUnit);

private:
    std::shared_mutex task_mtx_;
    std::string name_;
    float max_fps_{0.0};            // Maximum required frame rate among task queues
    float in_fps_{0.0};             // Actual frame rate sent to this distribution template
    size_t data_index_{0};          // Actual frame sequence sent to this distribution template
    util::FpsCalc input_fps_calc_;  // Input FPS calculator
    util::FpsCtrl out_fps_ctl_;     // Task frame rate control template
    int sign_{0};  // +1 for both queue registration and unregistration to mark changes in registrants
    std::uint64_t registration_generation_{1};
    const std::shared_ptr<const void> distributor_identity_{std::make_shared<int>(0)};
    const std::shared_ptr<RegistrationAuthority> registration_authority_{
        std::make_shared<RegistrationAuthority>()};
    std::vector<AlgDataTask> tasks_;
};

using AlgDataQueueDistributorPtr = std::shared_ptr<AlgDataQueueDistributor>;

}  // namespace cosmo
