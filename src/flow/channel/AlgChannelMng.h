// AlgChannelMng — manages the lifecycle of AlgChannel instances.

#pragma once

#include <map>
#include <memory>
#include <string>

#include "flow/action/ActionInstMngBase.h"
#include "flow/channel/AlgChannel.h"
#include "util/dto/ServerMsgTypes.h"
#include "util/dto/TaskCreateTypes.h"

namespace cosmo {
class AlgChannelMng : public IMngStatusProvider {
public:
    AlgChannelMng();
    ~AlgChannelMng();

    [[nodiscard]] AlgChannelPtr GetChannelInst(const std::string& channel_id);
    [[nodiscard]] AlgChannelPtr GetInst(const std::string& channel_id, const std::string& task_id,
                                        ActionNode& action);
    bool DeleteInst(AlgChannelPtr inst, const std::string& task_id);

    [[nodiscard]] std::vector<std::string> GetChannelTasks(const std::string& channel_id);

    void QueueStatus(std::vector<AlgActionDataQueueStatus>& que_status,
                     unsigned int duration_sec = 30) override;
    void ActionInfo(std::vector<ActionRuntimeInfo>& action_info) override;

    bool SetUrl(const std::string& channel_id, const std::string& url);
    bool SetVideoRepeatCount(const std::string& channel_id, int repeat_count);
    bool SetVideoFps(const std::string& channel_id, float fps);

    bool GetFrameInfo(const std::string& channel_id, bool& is_live, int64_t& index, int64_t& pts,
                      int64_t& frame_size, std::string& stream_url);
    void AddTaskChannel(MsgTaskCreateRecv task_create_recv);

    void DeleteTaskChannel(MsgTaskCancleRecv task_cancel_recv, const std::string& channel_id,
                           const std::string& algorithm_id);

    void GetCameraInfo(std::vector<MsgCameraInfo>& camera_infos);

private:
    std::shared_mutex mtx_;
    std::vector<AlgChannelPtr> channels_;
    std::map<std::string, MsgCameraInfo> camera_info_map_;
};
}  // namespace cosmo
