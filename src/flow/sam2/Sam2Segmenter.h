// SAM2 segmentation action for video stream pipeline.

#pragma once

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "flow/task/TaskBaseParam.h"
#include "infer/Sam2SegmenterUnify.h"

namespace cosmo {
// Use Sam2InputType from AISDK layer

struct Sam2SegmenterChannel {
    std::string channel;
    std::vector<std::string> task_list;
};

struct Sam2SegmenterParamEl {
    std::string task_id;
    std::string atomic_code;
    float fps{1.0};
    Sam2InputType input_type{Sam2InputType::BOX};
};

struct Sam2SegmenterParam {
    int param_modify_sign{0};
    int param_active_sign{-1};
    std::vector<Sam2SegmenterParamEl> param;
};

class Sam2Segmenter : public AlgActionBase {
public:
    explicit Sam2Segmenter(ActionNode& action);
    ~Sam2Segmenter();

    Sam2Segmenter(const Sam2Segmenter&)            = delete;
    Sam2Segmenter& operator=(const Sam2Segmenter&) = delete;

    bool Sam2SdkInit();

    // Modify parameters - modify based on existing parameters
    bool ModifyParam(const std::string& channel_id, const std::string& tgt_task_id,
                     std::vector<MsgDynamicKeyValue>& params) override;
    // Set parameters - clear previous parameters and set new ones completely
    bool SetParam(const std::string& channel_id, const std::string& tgt_task_id,
                  std::vector<MsgDynamicKeyValue>& params) override;

    void QueueStatus(std::vector<AlgActionDataQueueStatus>& que_status,
                     unsigned int duration_sec = 30) override;

    // Used when getting instance
    bool AddTask(const std::string& channel_id, const std::string& task);
    // Used when returning instance
    bool RemoveTask(const std::string& channel_id, const std::string& task);

    // Segmenter channel sharing - N channels use one segmenter. Which channels are currently occupying it
    [[nodiscard]] bool ChannelExist(const std::string& channel_id);
    // Segmenter channel task sharing - all tasks in 1 channel use one segmenter. Which tasks are currently
    // occupying it
    [[nodiscard]] bool TaskExist(const std::string& channel_id, const std::string& task);
    // Segmenter channel sharing - N channels use one segmenter
    // Segmenter channel task sharing - all tasks in 1 channel use one segmenter
    // Check if it can be used based on the above sharing rules
    [[nodiscard]] bool TaskIsFull();
    [[nodiscard]] bool TaskIsEmpty();

    [[nodiscard]] size_t ChannelCount();
    [[nodiscard]] size_t TaskCount();

    [[nodiscard]] std::string GetAlgCode() const {
        return alg_code_;
    }

protected:
    // Thread subclass override run
    void run() override;

private:
    void HandFrameBatch(const std::vector<AlgDataPtr>& alg_datas);
    bool ValidKey(MsgDynamicKeyValue& param);
    bool AnalysisKey(const std::string& channel_id, const std::string& tgt_task_id,
                     MsgDynamicKeyValue& param);
    [[nodiscard]] Sam2SegmenterParamEl FoundLocalParamByTask(const AlgTaskUnit& task) const;

    [[nodiscard]] Sam2SegmenterParamEl GetTaskParams(const std::string& tgt_task_id);

    std::string alg_code_;
    size_t max_reuse_count_{1};                       // Max number of channels that can reuse it
    size_t batch_count_{1};                           // Batch count
    std::vector<Sam2SegmenterChannel> channel_list_;  // Channels/tasks using it
    bool is_detector_inst_init_{false};
    Sam2SegmenterParam params_;

    // AISDK wrapper interface
    Sam2SegmenterUnifyPtr segmenter_;

    // Init retry control (prevent infinite retry after OOM)
    int init_retry_count_{0};            // Consecutive failure count
    int64_t last_init_fail_time_ms_{0};  // Timestamp of last failure (ms)
};
using Sam2SegmenterPtr = std::shared_ptr<Sam2Segmenter>;
}  // namespace cosmo
