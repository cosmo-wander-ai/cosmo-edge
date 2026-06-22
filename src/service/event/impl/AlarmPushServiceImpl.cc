// AlarmPushServiceImpl — Alarm push service — pushes alarm events to an external HTTP

#include "service/event/impl/AlarmPushServiceImpl.h"

#include <unistd.h>

#include <filesystem>

#include "service/algorithm/IAlgorithmQuery.h"
#include "service/detail/ServiceRegistry.h"
#include "service/event/IAlarmRecordService.h"
#include "service/event/IEventNotifier.h"
#include "service/system/IAppInfoService.h"
#include "service/system/IConfigReadService.h"
#include "util/CipherUtil.h"
#include "util/DateTimeFormat.h"
#include "util/FormatString.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/StringUtil.h"
#include "util/TimeUtil.h"
#include "util/UuidUtil.h"

namespace cosmo::service {

namespace {
    constexpr int kOfflinePushIntervalMs = 60 * 1000;           // 1 minute
    constexpr int64_t kRetryWindowMs     = 24 * 3600 * 1000LL;  // 24 hours
    constexpr int64_t kMinEventAgeMs     = 60 * 1000;           // 1 minute
}  // namespace

AlarmPushServiceImpl::AlarmPushServiceImpl() : event_post_que_("HTTP EVENT POST QUE", 100) {
    auto cfg_path = (std::filesystem::path(cosmo::path::GetCfgPath()) / conf_file_name_).string();
    if (!cosmo::util::LoadStructFromJsonFile(cfg_path, config_)) {
        LOG_WARN("Failed to load alarm push config from {}", cfg_path);
    }
}

AlarmPushServiceImpl::~AlarmPushServiceImpl() {
    if (timer_) {
        timer_->Cancel(offline_push_task_id_);
        timer_->Destroy();
    }
}

void AlarmPushServiceImpl::Init() {
    if (ServiceRegistry::Instance().Get<IConfigReadService>().IsNetworkModel()) {
        return;
    }

    timer_ = std::make_unique<cosmo::PeriodicTimer>("HTTP OFFLINE PUSH");
    timer_->Start();
    offline_push_task_id_ = timer_->Schedule([this]() { OfflinePush(); }, kOfflinePushIntervalMs);

    event_post_que_.SetProcessor([this](cosmo::CMsgOnEventsReq&& data) {
        if (config_.is_open) {
            OnEvents(std::move(data));
        } else {
            LOG_WARN("Push {}/{} But HTTP Client Not Registed", data.recordId, data.messageId);
        }
    });
    event_post_que_.SetChecker([this](const cosmo::CMsgOnEventsReq& data, const std::string& key) {
        return (key == data.messageId);
    });

    ServiceRegistry::Instance().Get<IEventNotifier>().SetEventPostQue(event_post_que_);
}

bool AlarmPushServiceImpl::IsEnabled() {
    return GetInfo().is_open;
}

std::string AlarmPushServiceImpl::GetUrl() {
    return GetInfo().url;
}

cosmo::util::ErrorEnum AlarmPushServiceImpl::SetPush(bool enable, const std::string& url) {
    AlarmPushParam info;
    info.is_open = enable;
    info.url     = url;

    std::lock_guard<std::shared_mutex> lock(mtx_);
    if ((info.is_open != config_.is_open) || (info.url != config_.url)) {
        config_ = info;
        SaveCfg();
    }
    return cosmo::util::ErrorEnum::Success;
}

void AlarmPushServiceImpl::SaveCfg() {
    auto path = (std::filesystem::path(cosmo::path::GetCfgPath()) / conf_file_name_).string();
    if (!cosmo::util::SaveStructToJsonFile(path, config_)) {
        LOG_WARN("Failed to save alarm push config to {}", path);
    }
}

bool AlarmPushServiceImpl::OfflinePushData(AlarmEventRecord& data) {
    if (event_post_que_.KeyInQueue(data.id)) {
        cosmo::AsyncQueueInfo status;
        event_post_que_.Status(status);
        LOG_INFO("Push {} But It Already In Queue {}, Len:{} Size:{}", data.id, status.name, status.queLength,
                 status.queSize);
        return true;
    }

    cosmo::CMsgOnEventsReq unit;
    unit.messageId      = data.id;
    unit.videoChannelId = data.cameraId;
    unit.timestamp      = std::to_string(data.timestamp);
    unit.itimestamp     = data.timestamp;
    unit.algorithmId    = data.algorithm_code;
    unit.algorithmCode  = data.algorithm_code;
    unit.algorithmName =
        ServiceRegistry::Instance().Get<IAlgorithmQuery>().GetAlgorithmName(unit.algorithmCode);
    unit.areaId   = data.areaId;
    unit.areaName = data.areaName;
    unit.recordId = data.trackId;
    if (!data.property.empty()) {
        if (cosmo::util::DecodeJson(data.property, unit.property)) {
            unit.bHaveProperty = true;
        }
    }
    std::vector<std::string> files;
    if (!cosmo::util::DecodeJson(data.extraFiles, files)) {
        LOG_WARN("Failed to decode extraFiles for event {}", data.id);
    }
    std::string local_file_path     = cosmo::path::GetEventPath(data.timestamp);
    std::string local_file_path_pre = (std::filesystem::path(local_file_path) / data.id).string();
    for (auto& file : files) {
        if (std::string::npos != file.find("full.")) {
            unit.fullPicture = local_file_path_pre + file;
        }
        if (std::string::npos != file.find("detect.")) {
            unit.detectedPicture = local_file_path_pre + file;
        }
        if (std::string::npos != file.find("orig.")) {
            unit.orignalPicture = local_file_path_pre + file;
        }
    }
    if (static_cast<int>(cosmo::EventRecordFlag::EventRecordVideoFlagHave) == data.videoFlag) {
        auto video_path = local_file_path_pre + "_video.mp4";
        if (cosmo::util::FileExist(video_path)) {
            unit.video           = local_file_path_pre + "_video.mp4";
            unit.videostructured = local_file_path_pre + ".json";
        }
    }
    unit.isRetryMessage = true;
    auto date_time      = cosmo::util::DateTime(unit.itimestamp / 1000);
    LOG_INFO("Retry Event [{}] Time:{} {} ", unit.messageId, date_time.Date().ToYMD(),
             date_time.Time().ToHMS());
    return OnEvents(std::move(unit));
}

void AlarmPushServiceImpl::OfflinePush() {
    AlarmPushParam param = GetInfo();
    if (!param.is_open) {
        return;
    }

    AlarmQueryCondition condition;
    auto now                    = cosmo::util::GetMilliseconds();
    condition.timeBegin         = now - kRetryWindowMs;
    condition.timeEnd           = now - kMinEventAgeMs;
    condition.pageNum           = 1;
    condition.pageSize          = 10;  // small batch to reduce peak memory
    condition.bExportTotalCount = false;
    condition.reportStatus      = 0;  // not yet reported
    auto rest = ServiceRegistry::Instance().Get<IAlarmRecordService>().QueryAlarmRecords(condition, 1);

    int fail_count = 0;
    for (auto& unit : rest.behaviorList) {
        // Skip remaining records after 3 consecutive failures (target server may be unreachable)
        if (fail_count >= 3) {
            LOG_WARN("OfflinePush: {} consecutive failures, skipping remaining records", fail_count);
            break;
        }
        if (!OfflinePushData(unit)) {
            fail_count++;
        } else {
            fail_count = 0;
        }
    }
}

AlarmPushParam AlarmPushServiceImpl::GetInfo() {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return config_;
}

void AlarmPushServiceImpl::AppendHeader(cosmo::network::http::HttpRequest& hrt) {
    hrt.SetContentType("application/json");
    std::string request_id("AIBOX_AAE_");
    request_id += cosmo::util::GenerateUUID();
    hrt.AppendHeader("RequestId", request_id);
}

template <typename IN, typename OUT>
bool AlarmPushServiceImpl::Submit(std::string url, IN& rgtIn, OUT& rgtOut, int timeout_sec) {
    cosmo::network::http::HttpStringHandler http_handler{};
    cosmo::network::http::HttpRequest http_req(url, &http_handler);

    AppendHeader(http_req);
    std::string json_result{};
    if (!cosmo::util::EncodeJson(rgtIn, json_result)) {
        LOG_ERRO("Msg:{} Enc Json failed", url);
        return false;
    }

    LOG_INFO("Msg:{} Content:{:.2048}{}", url, json_result, json_result.size() > 2048 ? " ..." : "");
    http_req.SetData(json_result);
    http_req.SetTimeout(timeout_sec);

    auto ret = http_req.Submit(cosmo::network::http::HttpRequestMethod::kPost);
    if (200 != ret) {
        LOG_ERRO("Msg:{} Submit Failed, Error Code:{}, Response:{:.2048}{}", url, ret, http_handler.GetData(),
                 http_handler.GetData().size() > 2048 ? " ..." : "");
        return false;
    }
    LOG_INFO("Msg:{} Get Response is:{}", url, http_handler.GetData());

    if (!cosmo::util::DecodeJson(http_handler.GetData(), rgtOut)) {
        LOG_ERRO("Msg:{} Get Response is:{} ", url, http_handler.GetData());
        return false;
    }
    return true;
}

std::string AlarmPushServiceImpl::ReadFileBase64(std::string file_name, size_t file_min_size,
                                                 size_t file_max_size) {
    if (file_name.empty()) {
        return "";
    }

    auto content = cosmo::util::ReadFile(file_name);
    if (content.size() < file_min_size) {
        return "";
    }

    if (content.size() > file_max_size) {
        LOG_WARN("File {} Size Is Large", file_name);
        return "";
    }
    return cosmo::util::EncBase64(content);
}

bool AlarmPushServiceImpl::OnEvents(cosmo::CMsgOnEventsReq&& reqEvent) {
    AlarmPushParam param = GetInfo();
    if (!param.is_open) {
        return false;
    }
    std::string url = param.url;

    cosmo::CMsgOnEventsRsp rsp;
    reqEvent.devId                         = ServiceRegistry::Instance().Get<IAppInfoService>().DevId();
    reqEvent.orignalPicture                = ReadFileBase64(reqEvent.orignalPicture);
    reqEvent.fullPicture                   = ReadFileBase64(reqEvent.fullPicture);
    reqEvent.detectedPicture               = ReadFileBase64(reqEvent.detectedPicture);
    reqEvent.property.face.image           = ReadFileBase64(reqEvent.property.face.image);
    reqEvent.property.recognition.LibImage = ReadFileBase64(reqEvent.property.recognition.LibImage);

    if (!Submit(url, reqEvent, rsp)) {
        LOG_WARN("Msg:{} recordId:{} Push To {} Failed", reqEvent.messageId, reqEvent.recordId, url);
        return false;
    }
    LOG_INFO("Msg:{} recordId:{} Push To {} Ok", reqEvent.messageId, reqEvent.recordId, url);
    ServiceRegistry::Instance().Get<IAlarmRecordService>().UpdateAlarmReportStatus(reqEvent.messageId, true);
    return true;
}

}  // namespace cosmo::service

#include <nlohmann/json.hpp>

#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::service {
void from_json(const nlohmann::json& j, AlarmPushParam& v) {
    if (j.contains("bOpen") && !j["bOpen"].is_null())
        j.at("bOpen").get_to(v.is_open);
    if (j.contains("url") && !j["url"].is_null())
        j.at("url").get_to(v.url);
}

void to_json(nlohmann::json& j, const AlarmPushParam& v) {
    j["bOpen"] = v.is_open;
    j["url"]   = v.url;
}

}  // namespace cosmo::service
