#pragma once

// Qwen3VLWorker — Qwen3VL vision-language model worker for video stream inference.

#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "flow/qwen3vl/OpenAiVlmClient.h"
#include "flow/task/TaskBaseParam.h"
#include "infer/Qwen3VLUnify.h"
#include "service/media/IVideoFrameCodec.h"
#include "service/media/IVideoFrameOSD.h"
#include "service/media/IVideoFrameTransform.h"

namespace cosmo {
enum class Qwen3VLGenerationStyle {
    RIGOROUS  = 0,  // Rigorous
    STANDARD  = 1,  // Standard
    DIVERGENT = 2,  // Divergent
    CUSTOM    = 3   // Custom
};

struct Qwen3VLWorkerChannel {
    std::string channel;
    std::vector<std::string> tasks;
};

struct Qwen3VLWorkerParamEl {
    std::string task_id;
    std::string atomic_code;
    float fps{1.0};
    std::string prompt;
    bool advanced_mode{false};
    Qwen3VLGenerationStyle generation_style{Qwen3VLGenerationStyle::STANDARD};
    Qwen3VLGenerationParam gen_param;  // Generation parameters for AISDK layer
    OpenAiVlmConfig open_ai_config;
};

struct Qwen3VLWorkerParam {
    int param_modify_sign{0};
    int param_active_sign{-1};
    std::vector<Qwen3VLWorkerParamEl> param;
};

struct Qwen3VLTaskContext {
    ActionAlgPtr action_alg;
    ActionNode action_node;
};

class Qwen3VLWorker : public AlgActionBase {
public:
    explicit Qwen3VLWorker(ActionNode &action);
    ~Qwen3VLWorker();

    bool Qwen3VLSdkInit();

    // Modify parameters - modify based on existing parameters
    bool ModifyParam(const std::string &channel_id, const std::string &task_id,
                     std::vector<MsgDynamicKeyValue> &params) override;
    // Set parameters - clear previous parameters and set new ones completely
    bool SetParam(const std::string &channel_id, const std::string &task_id,
                  std::vector<MsgDynamicKeyValue> &params) override;
    // Set area (for image cropping: use configured detection area instead of full image when no detection box
    // is available)
    bool SetArea(const std::string &channel_id, const std::string &task_id, std::vector<MsgTaskArea> &areas,
                 std::vector<MsgTaskArea> &shielded_areas) override;

    void QueueStatus(std::vector<AlgActionDataQueueStatus> &que_status,
                     unsigned int duration_sec = 30) override;
    void RegisterTaskContext(const std::string &task_id, ActionAlgPtr alg, const ActionNode &action) override;

    // Used when getting instance
    bool AddTask(const std::string &channel_id, const std::string &task);
    // Used when returning instance
    bool RemoveTask(const std::string &channel_id, const std::string &task);

    // Worker channel sharing - N channels use one worker. Which channels are occupying it
    bool ChannelExist(const std::string &channel_id);
    // Worker channel task sharing - all tasks in 1 channel use one worker. Which tasks are occupying it
    bool TaskExist(const std::string &channel_id, const std::string &task);
    // Worker channel sharing - N channels use one worker
    // Worker channel task sharing - all tasks in 1 channel use one worker
    // Check if it can be used based on the above sharing rules
    bool TaskIsFull();
    bool TaskIsEmpty();

    size_t ChannelCount();
    size_t TaskCount();

    std::string GetAlgCode() {
        return alg_code_;
    };

protected:
    // Thread subclass override run
    void run() override;

private:
    // Forward declaration for extracted HandFrameBatch helpers
    struct InferEntry;

    void HandFrameBatch(std::vector<AlgDataPtr> algDatas);
    void CollectInferEntries(std::vector<AlgDataPtr> &alg_datas, std::vector<InferEntry> &entries);
    bool RunBatchInference(std::vector<InferEntry> &entries, std::vector<Qwen3VLResult> &results);
    void ProcessInferResults(std::vector<InferEntry> &entries, std::vector<Qwen3VLResult> &results);
    bool ValidKey(MsgDynamicKeyValue &param);
    bool AnalysisKey(const std::string &channel_id, const std::string &task_id, MsgDynamicKeyValue &param);
    Qwen3VLWorkerParamEl FoundLocalParamByTask(const AlgTaskUnit &task);

    Qwen3VLWorkerParamEl GetTaskParams(const std::string &task_id);
    std::optional<Qwen3VLTaskContext> GetTaskContext(const std::string &task_id);
    std::string GetTaskFlowActionId(const std::string &task_id);

    // Set parameters based on generation style
    void ApplyGenerationStyle(Qwen3VLWorkerParamEl &param);

    std::string alg_code_;
    size_t max_reuse_count_{1};                       // Max number of channels that can reuse it
    size_t batch_count_{1};                           // Batch count
    std::vector<Qwen3VLWorkerChannel> channel_list_;  // Channels/tasks using it
    bool detector_inst_init_{false};
    int sign_register_{0};
    Qwen3VLWorkerParam params_;
    std::map<std::string, Qwen3VLTaskContext> task_contexts_;
    std::map<std::string, TaskBaseArea>
        task_areas_;  // Detection areas configured for each task (used for image cropping)

    // AISDK uses shared Qwen3VL instance via ILlmInferService
};
using Qwen3VLWorkerPtr = std::shared_ptr<Qwen3VLWorker>;
}  // namespace cosmo
