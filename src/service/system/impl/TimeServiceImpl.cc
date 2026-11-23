// TimeServiceImpl.cc — Implementation of TimeServiceImpl (NTP sync, timezone, config).

#include "service/system/impl/TimeServiceImpl.h"

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <utility>

#include "service/detail/ServiceRegistry.h"
#include "service/system/impl/NtpHelpers.h"
#include "util/DateTimeFormat.h"
#include "util/ErrorCode.h"
#include "util/JsonStructUtil.h"
#include "util/LimitedTypeJson.h"
#include "util/PathUtil.h"
#include "util/RuntimeIsolation.h"
#include "util/TimeUtil.h"

namespace cosmo::service {

// ---------------------------------------------------------------------------
// Embedded timezone city list (previously loaded from TimeZoneCitys.json)
// ---------------------------------------------------------------------------

static constexpr const char* kTimeZoneCitysJson = R"json(
{
    "timezonelist":[
        {"zh_cn":"(UTC-12:00) 国际日期变更线西","tz":"-12:00","areaID":1},
        {"zh_cn":"(UTC-11:00) 协调世界时-11","tz":"-11:00","areaID":2},
        {"zh_cn":"(UTC-10:00) 夏威夷","tz":"-10:00","areaID":3},
        {"zh_cn":"(UTC-09:00) 安克雷奇","tz":"-09:00","areaID":4},
        {"zh_cn":"(UTC-08:00) 下加利福尼亚州","tz":"-08:00","areaID":5},
        {"zh_cn":"(UTC-08:00) 太平洋时间（美国和加拿大）","tz":"-08:00","areaID":6},
        {"zh_cn":"(UTC-07:00) 奇瓦瓦,拉巴斯,马萨特兰","tz":"-07:00","areaID":7},
        {"zh_cn":"(UTC-07:00) 亚利桑那","tz":"-07:00","areaID":8},
        {"zh_cn":"(UTC-07:00) 山地时间（美国和加拿大）","tz":"-07:00","areaID":9},
        {"zh_cn":"(UTC-06:00) 中美洲","tz":"-06:00","areaID":10},
        {"zh_cn":"(UTC-06:00) 中部时间（美国和加拿大）","tz":"-06:00","areaID":11},
        {"zh_cn":"(UTC-06:00) 萨斯喀彻温","tz":"-06:00","areaID":12},
        {"zh_cn":"(UTC-06:00) 瓜达拉哈拉,墨西哥城,蒙特雷","tz":"-06:00","areaID":13},
        {"zh_cn":"(UTC-05:00) 波哥大,利马,基多","tz":"-05:00","areaID":14},
        {"zh_cn":"(UTC-05:00) 印地安那州（东部）","tz":"-05:00","areaID":15},
        {"zh_cn":"(UTC-05:00) 东部时间（美国和加拿大）","tz":"-05:00","areaID":16},
        {"zh_cn":"(UTC-04:30) 加拉加斯","tz":"-04:30","areaID":17},
        {"zh_cn":"(UTC-04:00) 大西洋时间（加拿大）","tz":"-04:00","areaID":18},
        {"zh_cn":"(UTC-04:00) 亚松森","tz":"-04:00","areaID":19},
        {"zh_cn":"(UTC-04:00) 乔治敦,拉巴斯,马瑙斯,圣胡安","tz":"-04:00","areaID":20},
        {"zh_cn":"(UTC-04:00) 库亚巴","tz":"-04:00","areaID":21},
        {"zh_cn":"(UTC-04:00) 圣地亚哥","tz":"-04:00","areaID":22},
        {"zh_cn":"(UTC-03:30) 纽芬兰","tz":"-03:30","areaID":23},
        {"zh_cn":"(UTC-03:00) 巴西利亚","tz":"-03:00","areaID":24},
        {"zh_cn":"(UTC-03:00) 格陵兰","tz":"-03:00","areaID":25},
        {"zh_cn":"(UTC-03:00) 卡宴,福塔雷萨","tz":"-03:00","areaID":26},
        {"zh_cn":"(UTC-03:00) 布宜诺斯艾利斯","tz":"-03:00","areaID":27},
        {"zh_cn":"(UTC-03:00) 蒙得维的亚","tz":"-03:00","areaID":28},
        {"zh_cn":"(UTC-02:00) 协调世界时-2","tz":"-02:00","areaID":29},
        {"zh_cn":"(UTC-01:00) 佛得角群岛","tz":"-01:00","areaID":30},
        {"zh_cn":"(UTC-01:00) 亚速尔群岛","tz":"-01:00","areaID":31},
        {"zh_cn":"(UTC+00:00) 卡萨布兰卡","tz":"+00:00","areaID":32},
        {"zh_cn":"(UTC+00:00) 蒙罗维亚,雷克雅未克","tz":"+00:00","areaID":33},
        {"zh_cn":"(UTC+00:00) 都柏林,爱丁堡,里斯本,伦敦","tz":"+00:00","areaID":34},
        {"zh_cn":"(UTC+00:00) 协调世界时","tz":"+00:00","areaID":35},
        {"zh_cn":"(UTC+01:00) 阿姆斯特丹,柏林,伯尔尼,罗马,斯德哥尔摩,维也纳","tz":"+01:00","areaID":36},
        {"zh_cn":"(UTC+01:00) 布鲁塞尔,哥本哈根,马德里,巴黎","tz":"+01:00","areaID":37},
        {"zh_cn":"(UTC+01:00) 中非西部","tz":"+01:00","areaID":38},
        {"zh_cn":"(UTC+01:00) 贝尔格莱德,布拉迪斯拉发,布达佩斯,卢布尔雅那,布拉格","tz":"+01:00","areaID":39},
        {"zh_cn":"(UTC+01:00) 萨拉热窝,斯科普里,华沙,萨格勒布","tz":"+01:00","areaID":40},
        {"zh_cn":"(UTC+01:00) 温得和克","tz":"+01:00","areaID":41},
        {"zh_cn":"(UTC+02:00) 雅典,布加勒斯特,伊斯坦布尔","tz":"+02:00","areaID":42},
        {"zh_cn":"(UTC+02:00) 赫尔辛基,基辅,里加,索非亚,塔林,维尔纽斯","tz":"+02:00","areaID":43},
        {"zh_cn":"(UTC+02:00) 开罗","tz":"+02:00","areaID":44},
        {"zh_cn":"(UTC+02:00) 大马士革","tz":"+02:00","areaID":45},
        {"zh_cn":"(UTC+02:00) 安曼","tz":"+02:00","areaID":46},
        {"zh_cn":"(UTC+02:00) 哈拉雷,比勒陀利亚","tz":"+02:00","areaID":47},
        {"zh_cn":"(UTC+02:00) 耶路撒冷","tz":"+02:00","areaID":48},
        {"zh_cn":"(UTC+02:00) 贝鲁特","tz":"+02:00","areaID":49},
        {"zh_cn":"(UTC+03:00) 巴格达","tz":"+03:00","areaID":50},
        {"zh_cn":"(UTC+03:00) 明斯克","tz":"+03:00","areaID":51},
        {"zh_cn":"(UTC+03:00) 利雅得","tz":"+03:00","areaID":52},
        {"zh_cn":"(UTC+03:00) 内罗毕","tz":"+03:00","areaID":53},
        {"zh_cn":"(UTC+03:30) 德黑兰","tz":"+03:30","areaID":54},
        {"zh_cn":"(UTC+04:00) 莫斯科,圣彼得堡,伏尔加格勒","tz":"+04:00","areaID":55},
        {"zh_cn":"(UTC+04:00) 第比利斯","tz":"+04:00","areaID":56},
        {"zh_cn":"(UTC+04:00) 埃里温","tz":"+04:00","areaID":57},
        {"zh_cn":"(UTC+04:00) 阿布扎比,马斯喀特","tz":"+04:00","areaID":58},
        {"zh_cn":"(UTC+04:00) 巴库","tz":"+04:00","areaID":59},
        {"zh_cn":"(UTC+04:00) 路易港","tz":"+04:00","areaID":60},
        {"zh_cn":"(UTC+04:30) 喀布尔","tz":"+04:30","areaID":61},
        {"zh_cn":"(UTC+05:00) 塔什干","tz":"+05:00","areaID":62},
        {"zh_cn":"(UTC+05:00) 伊斯兰堡,卡拉奇","tz":"+05:00","areaID":63},
        {"zh_cn":"(UTC+05:30) 斯里加亚渥登普拉","tz":"+05:30","areaID":64},
        {"zh_cn":"(UTC+05:30) 钦奈,加尔各答,孟买,新德里","tz":"+05:30","areaID":65},
        {"zh_cn":"(UTC+05:45) 加德满都","tz":"+05:45","areaID":66},
        {"zh_cn":"(UTC+06:00) 阿斯塔纳","tz":"+06:00","areaID":67},
        {"zh_cn":"(UTC+06:00) 达卡","tz":"+06:00","areaID":68},
        {"zh_cn":"(UTC+06:00) 叶卡捷琳堡","tz":"+06:00","areaID":69},
        {"zh_cn":"(UTC+06:30) 仰光","tz":"+06:30","areaID":70},
        {"zh_cn":"(UTC+07:00) 曼谷,河内,雅加达","tz":"+07:00","areaID":71},
        {"zh_cn":"(UTC+07:00) 新西伯利亚","tz":"+07:00","areaID":72},
        {"zh_cn":"(UTC+08:00) 克拉斯诺亚尔斯克","tz":"+08:00","areaID":73},
        {"zh_cn":"(UTC+08:00) 乌兰巴托","tz":"+08:00","areaID":74},
        {"zh_cn":"(UTC+08:00) 北京,重庆,香港,乌鲁木齐","tz":"+08:00","areaID":75},
        {"zh_cn":"(UTC+08:00) 佩思","tz":"+08:00","areaID":76},
        {"zh_cn":"(UTC+08:00) 吉隆坡,新加坡","tz":"+08:00","areaID":77},
        {"zh_cn":"(UTC+08:00) 台北","tz":"+08:00","areaID":78},
        {"zh_cn":"(UTC+09:00) 伊尔库茨克","tz":"+09:00","areaID":79},
        {"zh_cn":"(UTC+09:00) 首尔","tz":"+09:00","areaID":80},
        {"zh_cn":"(UTC+09:00) 大阪,札幌,东京","tz":"+09:00","areaID":81},
        {"zh_cn":"(UTC+09:30) 达尔文","tz":"+09:30","areaID":82},
        {"zh_cn":"(UTC+09:30) 阿德莱德","tz":"+09:30","areaID":83},
        {"zh_cn":"(UTC+10:00) 霍巴特","tz":"+10:00","areaID":84},
        {"zh_cn":"(UTC+10:00) 雅库茨克","tz":"+10:00","areaID":85},
        {"zh_cn":"(UTC+10:00) 布里斯班","tz":"+10:00","areaID":86},
        {"zh_cn":"(UTC+10:00) 堪培拉,墨尔本,悉尼","tz":"+10:00","areaID":87},
        {"zh_cn":"(UTC+11:00) 符拉迪沃斯托克","tz":"+11:00","areaID":88},
        {"zh_cn":"(UTC+11:00) 所罗门群岛,新喀里多尼亚","tz":"+11:00","areaID":89},
        {"zh_cn":"(UTC+12:00) 协调世界时+12","tz":"+12:00","areaID":90},
        {"zh_cn":"(UTC+12:00) 斐济,马绍尔群岛","tz":"+12:00","areaID":91},
        {"zh_cn":"(UTC+12:00) 马加丹","tz":"+12:00","areaID":92},
        {"zh_cn":"(UTC+12:00) 奥克兰,惠灵顿","tz":"+12:00","areaID":93},
        {"zh_cn":"(UTC+13:00) 努库阿洛法","tz":"+13:00","areaID":94},
        {"zh_cn":"(UTC+13:00) 萨摩亚群岛","tz":"+13:00","areaID":95}
    ]
})json";

// ---------------------------------------------------------------------------
// Persistence structures
// ---------------------------------------------------------------------------

struct TimeServiceImpl::TimeZonePersist {
    uint64_t tz_west{0};
    uint64_t tz_offset{800};
    uint64_t tz_citys{75};
    std::string content;
    std::string value{"+08:00"};

    // Custom JSON: preserve legacy keys for device config compatibility
    friend void to_json(nlohmann::json& j, const TimeZonePersist& p) {
        j = nlohmann::json{{"m_nTzWest", p.tz_west},
                           {"m_nTZOffset", p.tz_offset},
                           {"m_nTZCitys", p.tz_citys},
                           {"m_strContent", p.content},
                           {"m_value", p.value}};
    }
    friend void from_json(const nlohmann::json& j, TimeZonePersist& p) {
        TimeZonePersist defaults;
        p.tz_west   = j.value("m_nTzWest", defaults.tz_west);
        p.tz_offset = j.value("m_nTZOffset", defaults.tz_offset);
        p.tz_citys  = j.value("m_nTZCitys", defaults.tz_citys);
        p.content   = j.value("m_strContent", defaults.content);
        p.value     = j.value("m_value", defaults.value);
    }
};

struct TimeServiceImpl::NtpPersist {
    int enable{1};
    std::string ntp_server{"ntp.aliyun.com"};
    int ntp_port{123};
    int interval{60};

    // Custom JSON: preserve legacy keys for device config compatibility
    friend void to_json(nlohmann::json& j, const NtpPersist& p) {
        j = nlohmann::json{{"enable", p.enable},
                           {"ntpServer", p.ntp_server},
                           {"NTPPort", p.ntp_port},
                           {"interval", p.interval}};
    }
    friend void from_json(const nlohmann::json& j, NtpPersist& p) {
        NtpPersist defaults;
        p.enable     = j.value("enable", defaults.enable);
        p.ntp_server = j.value("ntpServer", defaults.ntp_server);
        p.ntp_port   = j.value("NTPPort", defaults.ntp_port);
        p.interval   = j.value("interval", defaults.interval);
    }
};

struct TimeServiceImpl::TimeZoneCityList {
    struct City {
        std::string zh_cn;
        std::string tz;
        size_t area_id;

        friend void to_json(nlohmann::json& j, const City& c) {
            j = nlohmann::json{{"zh_cn", c.zh_cn}, {"tz", c.tz}, {"areaID", c.area_id}};
        }
        friend void from_json(const nlohmann::json& j, City& c) {
            c.zh_cn   = j.value("zh_cn", std::string{});
            c.tz      = j.value("tz", std::string{});
            c.area_id = j.value("areaID", size_t{0});
        }
    };
    std::vector<City> timezone_list;

    friend void to_json(nlohmann::json& j, const TimeZoneCityList& l) {
        j = nlohmann::json{{"timezonelist", l.timezone_list}};
    }
    friend void from_json(const nlohmann::json& j, TimeZoneCityList& l) {
        l.timezone_list = j.value("timezonelist", std::vector<City>{});
    }
};

struct TimeServiceImpl::NtpOperation {
    bool IsCancelled() const {
        return cancelled.load(std::memory_order_acquire);
    }

    bool AttachSocket(int descriptor) {
        std::lock_guard<std::mutex> lock(socket_mutex);
        if (IsCancelled()) {
            return false;
        }
        socket = descriptor;
        return true;
    }

    void CloseSocket(int descriptor) {
        {
            std::lock_guard<std::mutex> lock(socket_mutex);
            if (socket == descriptor) {
                socket = -1;
            }
        }
        close(descriptor);
    }

    void Activate() {
        {
            std::lock_guard<std::mutex> lock(wait_mutex);
            activated = true;
        }
        wait_cv.notify_all();
    }

    bool WaitUntilActivated() {
        std::unique_lock<std::mutex> lock(wait_mutex);
        wait_cv.wait(lock, [this]() { return activated || IsCancelled(); });
        return activated && !IsCancelled();
    }

    bool WaitFor(std::chrono::milliseconds duration) {
        std::unique_lock<std::mutex> lock(wait_mutex);
        return wait_cv.wait_for(lock, duration, [this]() { return IsCancelled(); });
    }

    void ApplyTime(int64_t timestamp) {
        std::lock_guard<std::mutex> lock(apply_mutex);
        if (!IsCancelled()) {
            ApplyNtpTime(timestamp);
        }
    }

    void Cancel() {
        cancelled.store(true, std::memory_order_release);
        wait_cv.notify_all();
        {
            std::lock_guard<std::mutex> lock(socket_mutex);
            if (socket >= 0) {
                // The worker retains descriptor ownership. shutdown() interrupts a
                // blocking send/recv without exposing a descriptor-reuse race.
                (void)shutdown(socket, SHUT_RDWR);
            }
        }
        // Do not return while a successful calibration is applying system time.
        std::lock_guard<std::mutex> lock(apply_mutex);
    }

    std::atomic<bool> cancelled{false};
    std::mutex socket_mutex;
    int socket{-1};
    std::mutex wait_mutex;
    std::condition_variable wait_cv;
    bool activated{false};
    std::mutex apply_mutex;
};

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

TimeServiceImpl::TimeServiceImpl()
    : tz_persist_(std::make_unique<TimeZonePersist>()),
      ntp_persist_(std::make_unique<NtpPersist>()),
      city_list_(std::make_unique<TimeZoneCityList>()) {
    LoadConfig();
}

TimeServiceImpl::~TimeServiceImpl() {
    StopNtpTimer();
}

// ---------------------------------------------------------------------------
// NTP calibration — single-shot time sync
// ---------------------------------------------------------------------------

int64_t TimeServiceImpl::NtpCalibrate(const std::string& host, int port,
                                      const std::shared_ptr<NtpOperation>& operation) {
    if (runtime::IsIsolatedRuntime() || operation == nullptr || operation->IsCancelled()) {
        return 0;
    }

    int sockfd = ConnectNtpServer(
        host, port, [operation]() { return operation->IsCancelled(); },
        [operation](std::chrono::milliseconds duration) { (void)operation->WaitFor(duration); });
    if (sockfd < 0) {
        return 0;
    }
    if (!operation->AttachSocket(sockfd)) {
        close(sockfd);
        return 0;
    }
    const auto close_socket = [&operation, sockfd]() { operation->CloseSocket(sockfd); };

    // T1: client send time
    int64_t t[4] = {0};

    NtpPacket pkt;
    InitPacket(pkt);

    struct timeval now {};
    gettimeofday(&now, nullptr);
    TimevalToTimestamp(pkt.transmit_ts, now.tv_sec, now.tv_usec);
    t[0]                           = TimevalToNs100(now.tv_sec, now.tv_usec);
    const auto request_transmit_ts = pkt.transmit_ts;

    HtonPacket(pkt);
    if (send(sockfd, &pkt, sizeof(pkt), 0) != static_cast<ssize_t>(sizeof(pkt))) {
        LOG_INFO("{}", "NTP send failed");
        close_socket();
        return 0;
    }

    // Receive response
    NtpPacket resp{};
    const auto received = recv(sockfd, &resp, sizeof(resp), MSG_TRUNC);
    if (received != static_cast<ssize_t>(sizeof(resp))) {
        if (!operation->IsCancelled()) {
            LOG_INFO("{}", "NTP recv failed");
        }
        close_socket();
        return 0;
    }
    t[3] = GetTimevalueNs100();  // T4: client receive time
    close_socket();

    if (operation->IsCancelled()) {
        return 0;
    }

    NtohPacket(resp);
    if (!ValidateNtpResponse(resp, request_transmit_ts)) {
        LOG_WARN("{}", "NTP response validation failed");
        return 0;
    }
    t[1] = TimestampToNs100(resp.receive_ts);   // T2: server receive time
    t[2] = TimestampToNs100(resp.transmit_ts);  // T3: server transmit time

    // Standard NTP offset formula: ((T2-T1) + (T3-T4)) / 2
    return t[3] + ((t[1] - t[0]) + (t[2] - t[3])) / 2;
}

// ---------------------------------------------------------------------------
// NTP periodic sync timer
// ---------------------------------------------------------------------------

void TimeServiceImpl::StopNtpTimer() {
    std::thread thread;
    std::shared_ptr<NtpOperation> operation;
    {
        std::lock_guard<std::mutex> lock(ntp_mutex_);
        operation = std::move(ntp_operation_);
        thread    = std::move(ntp_thread_);
    }
    if (operation != nullptr) {
        operation->Cancel();
    }
    if (thread.joinable()) {
        thread.join();
    }
    LOG_INFO("{}", "NTP sync timer stopped");
}

bool TimeServiceImpl::StartNtpTimer(int interval_min, const std::string& host, int port) {
    if (runtime::IsIsolatedRuntime()) {
        return false;
    }
    int interval_sec = interval_min * 60;
    if (interval_sec < kMinSyncIntervalSec) {
        interval_sec = kMinSyncIntervalSec;
    }

    std::shared_ptr<NtpOperation> operation;
    std::thread thread;
    try {
        operation = std::make_shared<NtpOperation>();
        thread    = std::thread([operation, interval_sec, host, port]() {
            if (!operation->WaitUntilActivated()) {
                return;
            }

            LOG_INFO("NTP sync thread started, interval={}s host={} port={}", interval_sec, host, port);
            while (!operation->IsCancelled()) {
                if (auto timestamp = NtpCalibrate(host, port, operation)) {
                    operation->ApplyTime(timestamp);
                }
                if (operation->WaitFor(std::chrono::seconds(interval_sec))) {
                    break;
                }
            }
            LOG_INFO("{}", "NTP sync thread exiting");
        });
    } catch (const std::exception& e) {
        LOG_ERRO("Failed to start NTP sync thread: {}", e.what());
        return false;
    }

    // Creating the replacement can fail; retain the active timer until a fully
    // constructed replacement is ready.
    StopNtpTimer();
    {
        std::lock_guard<std::mutex> lock(ntp_mutex_);
        ntp_operation_ = operation;
        ntp_thread_    = std::move(thread);
    }
    operation->Activate();
    return true;
}

// ---------------------------------------------------------------------------
// Config loading & timezone management
// ---------------------------------------------------------------------------

void TimeServiceImpl::LoadConfig() {
    auto config_dir  = cosmo::path::GetCfgPath();
    tz_config_path_  = (std::filesystem::path(config_dir) / "TimeZoneInfo.json").string();
    ntp_config_path_ = (std::filesystem::path(config_dir) / "NTPStatusConfig.json").string();

    // Parse embedded timezone city list (no file I/O needed)
    try {
        auto doc    = nlohmann::json::parse(kTimeZoneCitysJson);
        *city_list_ = doc.get<TimeZoneCityList>();
    } catch (const std::exception& e) {
        LOG_ERRO("Failed to parse embedded timezone city list: {}", e.what());
    }

    NtpPersist loaded_ntp;
    std::error_code ntp_exists_error;
    const bool ntp_config_exists = std::filesystem::exists(ntp_config_path_, ntp_exists_error);
    const bool ntp_loaded        = cosmo::util::LoadStructFromJsonFile(ntp_config_path_, loaded_ntp);
    if (ntp_loaded && IsValidNtpConfig(loaded_ntp)) {
        *ntp_persist_ = std::move(loaded_ntp);
    } else {
        LOG_WARN("{}", "Invalid persisted NTP configuration, falling back to safe defaults");
        *ntp_persist_ = NtpPersist{};
        if (ntp_config_exists || ntp_exists_error) {
            // A malformed persisted configuration must not unexpectedly enable
            // external clock control after an upgrade or restart.
            ntp_persist_->enable = 0;
        }
        if (!SaveNtpConfig(*ntp_persist_)) {
            LOG_WARN("Failed to repair invalid NTP configuration: {}", ntp_config_path_);
        }
    }

    TimeZonePersist loaded_time_zone;
    const bool time_zone_loaded = cosmo::util::LoadStructFromJsonFile(tz_config_path_, loaded_time_zone);
    const int requested_city    = time_zone_loaded && loaded_time_zone.tz_citys <=
                                                       static_cast<uint64_t>(std::numeric_limits<int>::max())
                                      ? static_cast<int>(loaded_time_zone.tz_citys)
                                      : 75;

    TimeZonePersist time_zone;
    std::string environment;
    bool time_zone_valid = BuildTimeZoneConfig(requested_city, time_zone, environment);
    if (!time_zone_valid) {
        LOG_WARN("Invalid persisted timezone id {}, falling back to 75", loaded_time_zone.tz_citys);
        time_zone_valid = BuildTimeZoneConfig(75, time_zone, environment);
    }
    if (time_zone_valid) {
        *tz_persist_ = time_zone;
        if (!ApplyTimeZoneEnvironment(environment)) {
            LOG_ERRO("Failed to apply persisted timezone environment: {}", environment);
        }
        const bool time_zone_needs_repair =
            !time_zone_loaded || loaded_time_zone.tz_citys != time_zone.tz_citys ||
            loaded_time_zone.tz_west != time_zone.tz_west ||
            loaded_time_zone.tz_offset != time_zone.tz_offset ||
            loaded_time_zone.content != time_zone.content || loaded_time_zone.value != time_zone.value;
        if (time_zone_needs_repair) {
            if (!SaveTimeZoneConfig(time_zone)) {
                LOG_WARN("Failed to repair invalid timezone configuration: {}", tz_config_path_);
            }
        }
    } else {
        LOG_ERRO("{}", "Embedded timezone list does not contain the default timezone");
    }

    if (runtime::IsIsolatedRuntime()) {
        ntp_persist_->enable = 0;
    }
    if (ntp_persist_->enable == 1 &&
        !StartNtpTimer(ntp_persist_->interval, ntp_persist_->ntp_server, ntp_persist_->ntp_port)) {
        LOG_ERRO("{}", "Failed to restore persisted NTP timer");
        ntp_persist_->enable = 0;
        if (!SaveNtpConfig(*ntp_persist_)) {
            LOG_ERRO("{}", "Failed to persist disabled NTP state after timer startup failure");
        }
    }
}

bool TimeServiceImpl::IsValidNtpConfig(const NtpPersist& config) {
    return (config.enable == 0 || config.enable == 1) && IsValidNtpHost(config.ntp_server) &&
           config.ntp_port > 0 && config.ntp_port <= 65535 && config.interval >= 1 &&
           config.interval <= kMaxSyncIntervalMin;
}

bool TimeServiceImpl::BuildTimeZoneConfig(int city_id, TimeZonePersist& config,
                                          std::string& environment) const {
    auto it =
        std::find_if(city_list_->timezone_list.begin(), city_list_->timezone_list.end(),
                     [city_id](const auto& city) { return city.area_id == static_cast<size_t>(city_id); });
    if (it == city_list_->timezone_list.end()) {
        return false;
    }

    uint64_t is_west = 0;
    uint64_t offset  = 0;
    if (!ParseTimeZoneString(it->tz, is_west, offset)) {
        LOG_ERRO("Failed to parse time zone string: {}", it->tz);
        return false;
    }

    config.tz_citys  = static_cast<uint64_t>(city_id);
    config.content   = it->zh_cn;
    config.value     = it->tz;
    config.tz_west   = is_west;
    config.tz_offset = offset;

    char buffer[64]  = {};
    const auto count = snprintf(buffer, sizeof(buffer), "UTC%s%02zu:%02zu", is_west ? "+" : "-",
                                static_cast<size_t>(offset / 100), static_cast<size_t>(offset % 100));
    if (count <= 0 || static_cast<size_t>(count) >= sizeof(buffer)) {
        return false;
    }
    environment = buffer;
    return true;
}

bool TimeServiceImpl::ApplyTimeZoneEnvironment(const std::string& environment) {
    if (setenv("TZ", environment.c_str(), 1) != 0) {
        LOG_ERRO("Failed to set TZ environment to {}", environment);
        return false;
    }
    tzset();
    return true;
}

bool TimeServiceImpl::SaveNtpConfig(const NtpPersist& config) const {
    return cosmo::util::SaveStructToJsonFile(ntp_config_path_, config);
}

bool TimeServiceImpl::SaveTimeZoneConfig(const TimeZonePersist& config) const {
    return cosmo::util::SaveStructToJsonFile(tz_config_path_, config);
}

bool TimeServiceImpl::PersistConfig(const NtpPersist& ntp_config, const TimeZonePersist& time_zone_config,
                                    const NtpPersist& old_ntp_config) const {
    if (!SaveNtpConfig(ntp_config)) {
        LOG_ERRO("Failed to persist NTP configuration: {}", ntp_config_path_);
        return false;
    }
    if (!SaveTimeZoneConfig(time_zone_config)) {
        LOG_ERRO("Failed to persist timezone configuration: {}", tz_config_path_);
        if (!SaveNtpConfig(old_ntp_config)) {
            LOG_ERRO("Failed to roll back NTP configuration: {}", ntp_config_path_);
        }
        return false;
    }
    return true;
}

void TimeServiceImpl::RestoreConfig(const NtpPersist& ntp_config,
                                    const TimeZonePersist& time_zone_config) const {
    if (!SaveNtpConfig(ntp_config)) {
        LOG_ERRO("Failed to restore NTP configuration: {}", ntp_config_path_);
    }
    if (!SaveTimeZoneConfig(time_zone_config)) {
        LOG_ERRO("Failed to restore timezone configuration: {}", tz_config_path_);
    }
}

// ---------------------------------------------------------------------------
// ITimeService interface
// ---------------------------------------------------------------------------

TimeStatus TimeServiceImpl::GetTimeStatus(std::vector<TimeZoneItem>& zones) {
    std::lock_guard<std::mutex> lock(mutex_);
    TimeStatus ts;
    ts.ntp.enable   = ntp_persist_->enable;
    ts.ntp.server   = ntp_persist_->ntp_server;
    ts.ntp.interval = ntp_persist_->interval;
    ts.ntp.port     = ntp_persist_->ntp_port;

    std::transform(
        city_list_->timezone_list.begin(), city_list_->timezone_list.end(), std::back_inserter(zones),
        [](const auto& city) { return TimeZoneItem{city.zh_cn, city.tz, static_cast<int>(city.area_id)}; });

    ts.timestamp  = util::GetMilliseconds();
    auto date     = util::DateTime(ts.timestamp / 1000);
    ts.timeString = date.Date().ToYMD() + " " + date.Time().ToHMS();

    ts.timeZoneId    = tz_persist_->tz_citys;
    ts.timeZoneValue = tz_persist_->value;

    return ts;
}

util::ErrorEnum TimeServiceImpl::SyncNtp(const NtpConfig& config, int time_zone_id) {
    if (runtime::IsIsolatedRuntime()) {
        return util::ErrorEnum::OperationNotSupport;
    }
    if (time_zone_id < 1 || time_zone_id > 95 || config.enable < 0 || config.enable > 2) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    if (config.enable != 0 && (!IsValidNtpHost(config.server) || config.port <= 0 || config.port > 65535 ||
                               config.interval < 1 || config.interval > kMaxSyncIntervalMin)) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }

    if (config.enable == 2) {
        // Test mode — single calibration attempt
        auto operation = std::make_shared<NtpOperation>();
        if (NtpCalibrate(config.server, config.port, operation) == 0) {
            LOG_INFO("{}", "NTP test failed");
            return cosmo::util::ErrorEnum::TimeSyncFailed;
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);

    TimeZonePersist time_zone_config;
    std::string environment;
    if (!BuildTimeZoneConfig(time_zone_id, time_zone_config, environment)) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }

    const NtpPersist old_ntp_config            = *ntp_persist_;
    const TimeZonePersist old_time_zone_config = *tz_persist_;
    NtpPersist ntp_config                      = old_ntp_config;
    if (config.enable == 0) {
        ntp_config.enable = 0;
    } else if (config.enable == 1) {
        ntp_config.enable     = 1;
        ntp_config.ntp_server = config.server;
        ntp_config.ntp_port   = config.port;
        ntp_config.interval   = config.interval;
    }

    TimeZonePersist canonical_old_time_zone;
    std::string old_environment;
    if (!BuildTimeZoneConfig(static_cast<int>(old_time_zone_config.tz_citys), canonical_old_time_zone,
                             old_environment)) {
        return cosmo::util::ErrorEnum::SysErr;
    }

    if (!PersistConfig(ntp_config, time_zone_config, old_ntp_config)) {
        return cosmo::util::ErrorEnum::SysErr;
    }
    if (!ApplyTimeZoneEnvironment(environment)) {
        RestoreConfig(old_ntp_config, old_time_zone_config);
        return cosmo::util::ErrorEnum::SysErr;
    }

    *ntp_persist_ = ntp_config;
    *tz_persist_  = time_zone_config;

    if (config.enable == 1 &&
        !StartNtpTimer(ntp_config.interval, ntp_config.ntp_server, ntp_config.ntp_port)) {
        *ntp_persist_ = old_ntp_config;
        *tz_persist_  = old_time_zone_config;
        if (!ApplyTimeZoneEnvironment(old_environment)) {
            LOG_ERRO("Failed to restore timezone environment: {}", old_environment);
        }
        RestoreConfig(old_ntp_config, old_time_zone_config);
        return cosmo::util::ErrorEnum::SysErr;
    }
    if (config.enable == 0) {
        StopNtpTimer();
    }
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum TimeServiceImpl::SetTime(int64_t timestamp, int time_zone_id) {
    if (runtime::IsIsolatedRuntime()) {
        return util::ErrorEnum::OperationNotSupport;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (timestamp <= 0 || time_zone_id < 1 || time_zone_id > 95) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }

    TimeZonePersist time_zone_config;
    std::string environment;
    if (!BuildTimeZoneConfig(time_zone_id, time_zone_config, environment)) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }

    const NtpPersist old_ntp_config            = *ntp_persist_;
    const TimeZonePersist old_time_zone_config = *tz_persist_;
    NtpPersist ntp_config                      = old_ntp_config;
    ntp_config.enable                          = 0;

    TimeZonePersist canonical_old_time_zone;
    std::string old_environment;
    if (!BuildTimeZoneConfig(static_cast<int>(old_time_zone_config.tz_citys), canonical_old_time_zone,
                             old_environment)) {
        return cosmo::util::ErrorEnum::SysErr;
    }

    if (!PersistConfig(ntp_config, time_zone_config, old_ntp_config)) {
        return cosmo::util::ErrorEnum::SysErr;
    }
    if (!ApplyTimeZoneEnvironment(environment)) {
        RestoreConfig(old_ntp_config, old_time_zone_config);
        return cosmo::util::ErrorEnum::SysErr;
    }

    *ntp_persist_ = ntp_config;
    *tz_persist_  = time_zone_config;
    StopNtpTimer();

    auto result = cosmo::platform::SetSystemTime(timestamp);
    if (result != cosmo::util::ErrorEnum::Success) {
        LOG_WARN("set time failed:[{}], code:{}", timestamp, static_cast<int>(result));

        if (old_ntp_config.enable == 1 &&
            !StartNtpTimer(old_ntp_config.interval, old_ntp_config.ntp_server, old_ntp_config.ntp_port)) {
            // The candidate disabled state remains internally consistent when
            // restoration cannot create its required worker.
            LOG_ERRO("{}", "Failed to restore NTP timer after setting system time failed");
            return cosmo::util::ErrorEnum::SysErr;
        }
        *ntp_persist_ = old_ntp_config;
        *tz_persist_  = old_time_zone_config;
        if (!ApplyTimeZoneEnvironment(old_environment)) {
            LOG_ERRO("Failed to restore timezone environment: {}", old_environment);
        }
        RestoreConfig(old_ntp_config, old_time_zone_config);
        return result;
    }
    LOG_INFO("set time:[{}]", timestamp);
    return cosmo::util::ErrorEnum::Success;
}

}  // namespace cosmo::service
