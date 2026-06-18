// Target selection optimization

#pragma once

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "flow/overview/OverviewRecordAiRst.h"
#include "util/GeometricPos.h"

namespace cosmo {
class TargetChooseBest : public AlgActionBase {
public:
    TargetChooseBest(const std::string& taskId, ActionNode& action);
    ~TargetChooseBest();

    // Modify parameters based on existing ones
    bool ModifyParam(const std::string& channelId, const std::string& taskId,
                     std::vector<MsgDynamicKeyValue>& params) override;
    // Set parameters - clear previous ones and set fully new ones
    bool SetParam(const std::string& channelId, const std::string& taskId,
                  std::vector<MsgDynamicKeyValue>& params) override;

    MsgOverviewMem GetOverviewInfo(const std::string& channelId, const std::string& taskId,
                                   int64_t streamIndex = -1, int64_t from = -1, int64_t to = -1) override;

private:
    class TrackIdData;
    void HandFrame(AlgDataPtr algData) override;
    bool GetQualityAngle(const AiDetectRstEl& target, float& quality, FaceAngle& angle, bool has_related);
    void ChooseBest(VideoFramePtr frame, DataDetTrackClassifyPtr input, bool has_related);

    bool ValidKey(const MsgDynamicKeyValue& param) const;
    bool AnalysisKey(const MsgDynamicKeyValue& param) const;

    size_t frame_index_{0};
    size_t stream_index_{0};
    size_t timestamp_{0};
    int width_{-1};
    int height_{-1};
    std::map<unsigned, TrackIdData> track_id_status_;
    std::deque<DataDetTrackClassify> historys_;
    OverviewRecordAiRst overview_rec_inst_;
};
using TargetChooseBestPtr = std::shared_ptr<TargetChooseBest>;
}  // namespace cosmo
