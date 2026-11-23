// AiDetector.h — YOLO-based object detector with multi-channel sharing.

#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "flow/detect/AiDetectorFps.h"
#include "flow/detect/BenchAdmissionTable.h"
#include "flow/detect/BenchDetectorPlacement.h"
#include "flow/overview/OverviewRecordAiRst.h"
#include "flow/task/TaskBaseParam.h"
#include "infer/AiDetectorUnify.h"
#ifdef COSMO_NN_USE_AXERA_BACKEND
#include "flow/detect/NativeFaceOverflowCounter.h"
#include "flow/detect/NativeFaceWorkerInit.h"
#endif

// 1. Set confidence thresholds per label.
// 2. Filter detection results by confidence and distribute to task queues.

namespace cosmo {

struct AiDetectorParamEl {
    std::string task_id;
    std::vector<AiLabelParam> label_params;
};

struct AiDetectorParam {
    int param_modify_sign{0};
    int param_active_sign{-1};
    std::vector<AiDetectorParamEl> param;
};

class AiDetector : public AlgActionBase {
public:
    explicit AiDetector(ActionNode& action);
    ~AiDetector() override;

    bool AiSdkInit();
#ifdef COSMO_NN_USE_AXERA_BACKEND
    // Returns only an already initialized internal registration; never
    // creates one from user configuration or a model path.
    std::shared_ptr<const NativeFaceRegistration> NativeFaceRegistrationForQueue();
    enum class NativeProvisionStatus { NotConfigured, Ready, Failed };
    enum class NativeSelection { NotSelected, Selected, Invalid };
    NativeSelection NativeFaceSelection(const ActionNode* action = nullptr) const;
    // Internal lifecycle hook. The only path is the fixed protected app-root
    // deployment directory; no request may supply a path or model digest.
    NativeProvisionStatus PrepareProvisionedNativeFace(const std::string& task_scene,
                                                       const ActionAlg* task_graph);
#endif

    // Modify parameters — update on top of existing parameters
    bool ModifyParam(const std::string& channel_id, const std::string& taskId,
                     std::vector<MsgDynamicKeyValue>& params) override;
    // Set parameters — clear previous parameters, set all new parameters
    bool SetParam(const std::string& channel_id, const std::string& taskId,
                  std::vector<MsgDynamicKeyValue>& params) override;

    // Set areas — clear previous areas, set all new areas
    bool SetArea(const std::string& channel_id, const std::string& taskId, std::vector<MsgTaskArea>& areas,
                 std::vector<MsgTaskArea>& shieldedAreas) override;

    // Get history records (from: start frame, to: end frame)
    std::vector<DataDetTrackClassify> GetHistory(const std::string& channel_id, const std::string& taskId,
                                                 int64_t from, int64_t ts, int64_t to) override;

    // Get overlay/overview information
    MsgOverviewMem GetOverviewInfo(const std::string& channel_id, const std::string& taskId,
                                   int64_t streamIndex = -1, int64_t from = -1, int64_t to = -1) override;

    void QueueStatus(std::vector<AlgActionDataQueueStatus>& que_status,
                     unsigned int duration_sec = 30) override;

    void Stop() override;
#ifdef COSMO_NN_USE_AXERA_BACKEND
    [[nodiscard]] bool Start() override;
#endif

    // Add task when acquiring an instance. requested_fps <= 0 means unconfigured (normalized inside).
    bool AddTask(const std::string& channel_id, const std::string& task, float requested_fps);
    // Add task when acquiring an instance (legacy: fps unconfigured).
    bool AddTask(const std::string& channel_id, const std::string& task);
    // Remove task when releasing an instance
    bool RemoveTask(const std::string& channel_id, const std::string& task);

    // Check if a channel already uses this shared detector
    bool ChannelExist(const std::string& channel_id) const;
    // Check if a task already exists within a channel on this detector
    bool TaskExist(const std::string& channel_id, const std::string& task) const;
    // Check if max channel reuse count is reached (detector shared across N channels)
    bool TaskIsFull() const;
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
    // Caller holds the placement manager lock, just as for ChannelCount().
    bool HasOnlyBenchmarkBindings() const {
        if (channel_list_.empty() || alg_code_ != "9275710")
            return false;
        for (const auto& ch : channel_list_) {
            if (ch.tasks.empty())
                return false;
            for (const auto& task : ch.tasks)
                if (!IsScenarioBenchDetectorTask(alg_code_, ch.channel, task.task))
                    return false;
        }
        return true;
    }
#endif
    bool TaskIsEmpty() const;

    size_t ChannelCount() const;
    size_t TaskCount() const;

    // fps-aware placement helpers. Callers must hold the owning MultiChannelActionMng::m_mtx:
    // these read channel_list_, which is mutated under that lock (channel_list_ itself is not
    // guarded by mtx; mtx only protects task_histories_/task_areas_/overview_rec_insts_).
    float NormalizeRequestedFps(float fps) const;
    float AssignedFps() const;  // Sum of each channel's max-task-fps
    float DeltaFpsForTask(const std::string& channel_id, float requested_fps) const;
    bool CanAccept(const std::string& channel_id, float requested_fps) const;
    float GetInstanceFpsBudget() const {
        return instance_fps_budget_;
    }

    std::string GetDetectorName() const {
        return name_;
    }
    std::string GetAlgCode() const {
        return alg_code_;
    }
    std::optional<InferenceInputSize> GetInputSize() const {
        return input_size_;
    }

protected:
    // Worker thread entry point (override from base class)
    void run() override;

private:
    using AlgActionBase::GetName;

    // taskId/streamIndex keep camelCase to avoid shadowing AlgActionBase members task_id/stream_index.
    void HandFrames(std::vector<AlgDataPtr> alg_datas);
    bool ValidKey(MsgDynamicKeyValue& param);
    bool AnalysisKey(const std::string& channel_id, const std::string& taskId, MsgDynamicKeyValue& param);
    bool SetConfidenceToLocal(const std::string& channel_id, const std::string& taskId,
                              AiConfidence& confidence);
    bool SetTargetPosToLocal(const std::string& channel_id, const std::string& taskId,
                             const std::string& in_label, TargetPosition pos);
    AiDetectorParamEl FoundLocalParamByTask(const AlgTaskUnit& task);
    void FindActiveConfidence(const std::vector<AiDetectorParamEl>& shouldActiveParams);
    void CheckAndActivateConfidence();

    AlgDataPtr ConfidenceFilter(AlgDataPtr data_ptr, const std::string& taskId);
    TargetPosition GetTaskLabelPos(const std::string& label, const AiDetectorParamEl& taskLabels);
    AiDetectorParamEl GetTaskLabelParams(const std::string& taskId);
    void SignTargetAreas(AlgDataPtr data_ptr, const std::string& taskId);
    void TargetAddArea(AiDetectRstEl& target, TargetPosition pos, TargetAreaType type, MsgTaskArea& area,
                       int pic_width, int pic_height, bool associated_area = false,
                       const std::string& main_area_id = "");
    void TargetAddLine(AiDetectRstEl& target, TargetPosition pos, MsgTaskArea& area, int pic_width,
                       int pic_height, bool associated_area = false, const std::string& main_area_id = "");

    // Record detection history (with aging) for overlay information
    void RecordHistory(AlgDataPtr data_ptr, const std::string& taskId);

    void AddOverviewTask(const std::string& taskId);
    void OverviewRecord(const std::string& taskId, DataDetTrackClassifyPtr detRet);

    // Adjust processing queue size based on input frame rate
    void SetProcQueSize();

private:
#ifdef COSMO_NN_USE_AXERA_BACKEND
    struct NativePrepared;
    std::unique_ptr<NativeFaceWorkerInit::Prepared> PrepareNativeFaceOnWorker(
        std::shared_ptr<nn::axera::face_binding::Binding> binding,
        std::shared_ptr<const nn::axera::face_binding::Capability> capability, std::uint64_t task_generation,
        std::string scene_id, float scene_threshold);
    std::unique_ptr<NativeFaceWorkerInit::Prepared> PrepareE24OnWorker(
        std::shared_ptr<const std::vector<std::uint8_t>> bytes, std::uint64_t generation, std::string scene);
    std::unique_ptr<NativeFaceWorkerInit::Prepared> PrepareBenchmarkPersonOnWorker(
        std::unique_ptr<nn::axera::benchmark::PersonExecutor> executor, std::uint64_t task_generation,
        std::string scene_id);
    std::unique_ptr<NativeFaceWorkerInit::Prepared> PrepareNativePersonOnWorker(
        std::shared_ptr<nn::axera::person_binding::Binding> binding,
        std::shared_ptr<const nn::axera::person_binding::Capability> capability,
        std::uint64_t task_generation, std::string scene_id, float scene_threshold,
        bool person_template = false);
    std::unique_ptr<NativeFaceWorkerInit::Prepared> PrepareNativeFireOnWorker(
        std::shared_ptr<nn::axera::fire_binding::Binding> binding,
        std::shared_ptr<const nn::axera::fire_binding::Capability> capability, std::uint64_t task_generation,
        std::string scene_id);
    std::unique_ptr<NativeFaceWorkerInit::Prepared> PrepareNativeVehicleOnWorker(
        std::shared_ptr<nn::axera::vehicle_binding::Binding> binding,
        std::shared_ptr<const nn::axera::vehicle_binding::Capability> capability,
        std::uint64_t task_generation, std::string scene_id);
#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
    friend class TaskBase;
    // TaskBase is the only future installer; currently no admission is created.
    std::shared_ptr<const NativeHelmetAdmission> native_helmet_admission_;
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
    // Native production routes retain the original single admission.
    BenchAdmissionTable<NativeHelmetAdmission> bench_helmet_admissions_;
#endif
#endif
    std::mutex native_registration_mutex_;
    std::mutex native_lifecycle_mutex_;
    NativeFaceWorkerInit native_worker_init_;
    native_face::OverflowCounter native_face_overflow_count_;
    native_face::OverflowCounter native_person_overflow_count_{util::ErrorEnum::AI_PERSON_OUTPUT_OVERFLOW};
    bool native_person_registered_{false};
    std::optional<std::uint64_t> native_face_capacity_;
    std::shared_ptr<const NativeFaceRegistration> native_registration_;
    std::string native_scene_id_;
    float native_scene_threshold_{0.f};
    std::optional<float> native_person_min_threshold_;
    std::optional<std::array<nn::axera::fire_binding::ClassPolicy, 2>> native_fire_policies_;
    std::optional<std::array<nn::axera::vehicle_binding::ClassPolicy, 3>> native_vehicle_policies_;
#endif
    std::string alg_code_;
    std::optional<InferenceInputSize> input_size_;
    std::string name_;
    int init_retry_count_{0};
    int64_t last_init_fail_time_ms_{0};
    std::vector<std::string> labels_;
    size_t max_reuse_count_{1};                             // Max channels sharing this detector
    float instance_fps_budget_{kDefaultInstanceFpsBudget};  // Per-instance fps budget for placement
    ai_detector_fps::ReuseProfile reuse_profile_;           // fps-threshold reuse profile for placement
    size_t batch_count_{1};                                 // Batch size for inference
    std::vector<AiDetectorChannel> channel_list_;           // Channels/tasks using this detector
    bool is_detector_inst_initialized_{false};
    AiDetectorUnifyPtr detector_;  // Detector inference instance
    int sign_register_{0};         // Tracks distributor sign changes to trigger confidence re-activation
    AiDetectorParam params_;
    int active_confidence_modify_sign_{0};
    int active_confidence_active_sign_{-1};
    std::vector<AiConfidence> active_confidence_;
    std::vector<AiDetectorParamEl> active_task_confidence_;
    std::map<std::string, TaskBaseArea> task_areas_;
    std::map<std::string, std::deque<DataDetTrackClassify>> task_histories_;
    std::map<std::string, OverviewRecordAiRstPtr> overview_rec_insts_;
};
using AiDetectorPtr = std::shared_ptr<AiDetector>;
}  // namespace cosmo
