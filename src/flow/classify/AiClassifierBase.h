// AI classifier action base class for shared classification logic.

#pragma once

#include <deque>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "flow/overview/OverviewRecordAiRst.h"
#include "flow/task/TaskBaseParam.h"
#include "infer/AiClassifierUnify.h"

namespace cosmo {

class AiClassifierBase : public AlgActionBase {
public:
    AiClassifierBase(AlgActionType actionType, const std::string& taskId, ActionNode& action,
                     const std::string& logTag, const std::string& overviewRecordKey);
    virtual ~AiClassifierBase();

    bool AiSdkInit();

    // Retrieve history records. from: start frame, to: end frame.
    std::vector<DataDetTrackClassify> GetHistory(const std::string& channelId, const std::string& taskId,
                                                 int64_t from, int64_t ts, int64_t to) override;

    // Retrieve overview overlay information.
    MsgOverviewMem GetOverviewInfo(const std::string& channelId, const std::string& taskId,
                                   int64_t streamIndex = -1, int64_t from = -1, int64_t to = -1) override;

    bool SetArea(const std::string& channelId, const std::string& taskId, std::vector<MsgTaskArea>& areas,
                 std::vector<MsgTaskArea>& shieldedAreas) override;

protected:
    // Thread entry point overridden from AlgActionBase.
    void run() override;

    virtual bool CheckDataAvailable(AlgDataPtr algData)         = 0;
    virtual void HandFramesEx(std::vector<AlgDataPtr> algDatas) = 0;

    // Record history with aging for overlay information retrieval.
    void RecordHistory(AlgDataPtr dataPtr);

    // Adjust processing queue size based on input frame rate.
    void SetProcQueSize();

    std::string log_tag_;
    std::string alg_code_;
    size_t batch_count_{8};            // Batch size
    AiClassifierUnifyPtr classifier_;  // Classifier instance
    std::deque<DataDetTrackClassify> historys_;
    OverviewRecordAiRst overview_rec_inst_;
    TaskBaseArea task_area_;
};

using AiClassifierBasePtr = std::shared_ptr<AiClassifierBase>;

}  // namespace cosmo
