// Alarm push service — pushes alarm events to an external HTTP
// server with offline retry and queue-based delivery.

#pragma once

#include <list>
#include <mutex>

#include "network/http/HttpRequest.h"
#include "service/event/IAlarmPushService.h"
#include "service/event/dto/AlarmRecordTypes.h"
#include "util/AsyncQueue.h"
#include "util/Log.h"
#include "util/PeriodicTimer.h"
#include "util/dto/ClientMsgEvent.h"

namespace cosmo::service {

struct AlarmPushParam {
    bool is_open{false};
    std::string url;
    friend void to_json(nlohmann::json& j, const AlarmPushParam& v);
    friend void from_json(const nlohmann::json& j, AlarmPushParam& v);
};

/// Pushes alarm events to an external HTTP server, with offline retry.
/// Implements IAlarmPushService and is registered in ServiceRegistry.
class AlarmPushServiceImpl final : public IAlarmPushService {
public:
    AlarmPushServiceImpl();
    ~AlarmPushServiceImpl() override;

    // IAlarmPushService
    void Init() override;
    bool IsEnabled() override;
    std::string GetUrl() override;
    cosmo::util::ErrorEnum SetPush(bool enable, const std::string& url) override;

private:
    AlarmPushParam GetInfo();
    void SaveCfg();
    void AppendHeader(cosmo::network::http::HttpRequest& hrt);
    template <typename IN, typename OUT>
    bool Submit(std::string url, IN& rgtIn, OUT& rgtOut, int timeout_sec = 10);
    std::string ReadFileBase64(std::string file_name, size_t file_min_size = 64,
                               size_t file_max_size = 6 * 1024 * 1024);
    bool OnEvents(cosmo::CMsgOnEventsReq&& reqEvent);
    void OfflinePush();
    bool OfflinePushData(AlarmEventRecord& data);
    std::shared_mutex mtx_;
    std::string conf_file_name_{"HttpAlarmPush.json"};
    std::unique_ptr<cosmo::PeriodicTimer> timer_;
    cosmo::TaskId offline_push_task_id_{cosmo::kInvalidTaskId};
    AlarmPushParam config_;
    cosmo::AsyncQueue<cosmo::CMsgOnEventsReq> event_post_que_;
};

}  // namespace cosmo::service
