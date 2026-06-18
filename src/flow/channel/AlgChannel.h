// AlgChannel — per-channel orchestration of demuxer and decoder pipelines.

#pragma once

#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "flow/channel/AlgChannelDecode.h"
#include "flow/channel/AlgChannelDemux.h"
#include "util/dto/CosmoFwd.h"

namespace cosmo {

class AlgChannel : public AlgActionBase {
public:
    AlgChannel(const std::string& channel_id, const std::string& init_task_id, ActionNode& action,
               const std::string& url = "");
    ~AlgChannel();

    void Start() override;
    void Stop() override;

    // dataType selects pre-decode (ChannelDataOrig: H.264/H.265) or post-decode data.
    bool RegistTaskQueue(AlgTaskUnit& param) override;
    bool RemoveTaskQueue(AlgTaskUnit& param) override;
    int ForceRemoveByTaskId(const std::string& target_task_id) override;

    void QueueStatus(std::vector<AlgActionDataQueueStatus>& que_status,
                     unsigned int duration_sec = 30) override;
    void ActionInfo(std::vector<ActionRuntimeInfo>& action_info) override;

    void AddViewerPacketQueue(std::shared_ptr<AsyncQueue<VideoPacketPtr>> async_packet_queue);
    void RemoveViewerPacketQueue(std::shared_ptr<AsyncQueue<VideoPacketPtr>> async_packet_queue);
    void AddViewerFrameQueue(const std::string& alg_id, AsyncQueue<VideoFramePtr>& async_frame_queue);
    void RemoveViewerFrameQueue(const std::string& alg_id);

    // Modify parameters incrementally on top of existing values.
    bool ModifyParam(const std::string& channel_id, const std::string& param_task_id,
                     std::vector<MsgDynamicKeyValue>& params) override;
    bool SetParam(const std::string& channel_id, const std::string& param_task_id,
                  std::vector<MsgDynamicKeyValue>& params) override;

    bool SetUrl(const std::string& url);
    bool SetVideoRepeatCount(int repeat_count);
    bool SetVideoFps(float fps);
    bool SetPollChannel(const std::string& channel_id);
    [[nodiscard]] std::string GetUrl() const;

    // Get stream fetch status.
    [[nodiscard]] util::ErrorEnum GetUrlStatus() const;
    [[nodiscard]] bool GetAttr(MsgCameraAttr& attr);

    [[nodiscard]] VideoFramePtr CaptureImage(int timeout_ms = 3000);

    [[nodiscard]] bool GetFrameInfo(bool& is_live, int64_t& index, int64_t& pts, int64_t& frame_size,
                                    std::string& stream_url);

    void AddTask(const std::string& new_task_id);
    // Remove task and return true when no tasks remain (channel deletable).
    bool RmvTask(const std::string& target_task_id);

    [[nodiscard]] std::vector<std::string> GetTasks() const;

    [[nodiscard]] bool IsDataActive() const;

    // Get demux and decode duration info.
    std::vector<std::pair<std::string, util::DurationStatInfo>> GetChannelDurations(int durationMs = 5000) {
        return {{"Decode", decoder_.GetDurationInfo(durationMs)}};
    }

    // frameSeq: frame sequence (input)
    // frameTimestamp: timestamp (input)
    // startFrameSeq: start frame sequence (output)
    bool RecordMp4(RecordParam& record_param);

    void Quit();

private:
    mutable std::shared_mutex mtx_;

    std::string sub_channel_id_;  // Actual channel ID for polling tasks
    std::string url_;
    bool is_started_{false};
    util::ErrorEnum action_status_{util::ErrorEnum::Success};
    std::vector<std::string> tasks_;
    AlgTaskUnit regist_task_;
    AlgChannelDemux demuxer_;   // FFmpeg demux
    AlgChannelDecode decoder_;  // Video decode
};
using AlgChannelPtr = std::shared_ptr<AlgChannel>;
}  // namespace cosmo
