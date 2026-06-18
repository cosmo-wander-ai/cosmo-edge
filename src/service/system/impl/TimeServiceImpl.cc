// TimeServiceImpl.cc — Implementation of TimeServiceImpl (NTP sync, timezone, config).

#include "service/system/impl/TimeServiceImpl.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iterator>
#include <nlohmann/json.hpp>

#include "service/detail/ServiceRegistry.h"
#include "service/system/impl/NtpHelpers.h"
#include "util/DateTimeFormat.h"
#include "util/ErrorCode.h"
#include "util/LimitedTypeJson.h"
#include "util/PathUtil.h"
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

int64_t TimeServiceImpl::NtpCalibrate(const std::string& host, int port) {
    int sockfd = ConnectNtpServer(host, port);
    if (sockfd < 0) {
        return 0;
    }

    // T1: client send time
    int64_t t[4] = {0};

    NtpPacket pkt;
    InitPacket(pkt);

    struct timeval now {};
    gettimeofday(&now, nullptr);
    TimevalToTimestamp(pkt.transmit_ts, now.tv_sec, now.tv_usec);
    t[0] = TimevalToNs100(now.tv_sec, now.tv_usec);

    HtonPacket(pkt);
    if (send(sockfd, &pkt, sizeof(pkt), 0) < 0) {
        LOG_INFO("{}", "NTP send failed");
        close(sockfd);
        return 0;
    }

    // Receive response
    NtpPacket resp{};
    if (recv(sockfd, &resp, sizeof(resp), 0) < 0) {
        LOG_INFO("{}", "NTP recv failed");
        close(sockfd);
        return 0;
    }
    t[3] = GetTimevalueNs100();  // T4: client receive time
    close(sockfd);

    NtohPacket(resp);
    t[1] = TimestampToNs100(resp.receive_ts);   // T2: server receive time
    t[2] = TimestampToNs100(resp.transmit_ts);  // T3: server transmit time

    // Standard NTP offset formula: ((T2-T1) + (T3-T4)) / 2
    return t[3] + ((t[1] - t[0]) + (t[2] - t[3])) / 2;
}

// ---------------------------------------------------------------------------
// NTP periodic sync timer
// ---------------------------------------------------------------------------

void TimeServiceImpl::StopNtpTimer() {
    {
        std::lock_guard<std::mutex> lock(ntp_mutex_);
        ntp_stop_ = true;
    }
    ntp_cv_.notify_all();
    if (ntp_thread_.joinable()) {
        ntp_thread_.join();
    }
    LOG_INFO("{}", "NTP sync timer stopped");
}

void TimeServiceImpl::StartNtpTimer(int interval_min, const std::string& host, int port) {
    StopNtpTimer();

    int interval_sec = interval_min * 60;
    if (interval_sec < kMinSyncIntervalSec) {
        interval_sec = kMinSyncIntervalSec;
    }

    {
        std::lock_guard<std::mutex> lock(ntp_mutex_);
        ntp_stop_ = false;
    }

    ntp_thread_ = std::thread([this, interval_sec, host, port]() {
        LOG_INFO("NTP sync thread started, interval={}s host={} port={}", interval_sec, host, port);

        // Initial sync immediately
        if (auto ts = NtpCalibrate(host, port)) {
            ApplyNtpTime(ts);
        }

        std::unique_lock<std::mutex> lock(ntp_mutex_);
        while (!ntp_stop_) {
            ntp_cv_.wait_for(lock, std::chrono::seconds(interval_sec));
            // cppcheck-suppress knownConditionTrueFalse
            if (ntp_stop_) {
                break;
            }
            // Periodic sync (release lock during network I/O)
            lock.unlock();
            if (auto ts = NtpCalibrate(host, port)) {
                ApplyNtpTime(ts);
            }
            lock.lock();
        }

        LOG_INFO("{}", "NTP sync thread exiting");
    });
}

// ---------------------------------------------------------------------------
// Config loading & timezone management
// ---------------------------------------------------------------------------

void TimeServiceImpl::LoadConfig() {
    auto config_dir  = cosmo::path::GetCfgPath();
    tz_config_path_  = (std::filesystem::path(config_dir) / "TimeZoneInfo.json").string();
    ntp_config_path_ = (std::filesystem::path(config_dir) / "NTPStatusConfig.json").string();

    ReadJson(*tz_persist_, tz_config_path_);
    ReadJson(*ntp_persist_, ntp_config_path_);

    // Parse embedded timezone city list (no file I/O needed)
    try {
        auto doc    = nlohmann::json::parse(kTimeZoneCitysJson);
        *city_list_ = doc.get<TimeZoneCityList>();
    } catch (const std::exception& e) {
        LOG_ERRO("Failed to parse embedded timezone city list: {}", e.what());
    }

    // Single source of truth for TZ environment setup during startup
    ApplyTimeZone(tz_persist_->tz_citys);
}

void TimeServiceImpl::ApplyTimeZone(int city_id) {
    std::string tz_value  = tz_persist_->value;
    std::string city_name = tz_persist_->content;

    auto it =
        std::find_if(city_list_->timezone_list.begin(), city_list_->timezone_list.end(),
                     [city_id](const auto& city) { return city.area_id == static_cast<size_t>(city_id); });
    if (it != city_list_->timezone_list.end()) {
        tz_value  = it->tz;
        city_name = it->zh_cn;
    }

    uint64_t is_west = 0;
    uint64_t offset  = 0;
    if (ParseTimeZoneString(tz_value, is_west, offset)) {
        tz_persist_->tz_citys  = city_id;
        tz_persist_->content   = city_name;
        tz_persist_->value     = tz_value;
        tz_persist_->tz_west   = is_west;
        tz_persist_->tz_offset = offset;

        // NOTE: setenv and tzset affect process global state.
        // It is not strictly thread-safe with localtime() calls in other threads.
        char buffer[64] = {0};
        snprintf(buffer, sizeof(buffer), "UTC%s%02zu:%02zu", is_west ? "+" : "-",
                 static_cast<size_t>(offset / 100), static_cast<size_t>(offset % 100));
        setenv("TZ", buffer, 1);
        tzset();

        SaveTimeZoneConfig();
    } else {
        LOG_ERRO("Failed to parse time zone string: {}", tz_value);
    }
}

void TimeServiceImpl::SaveNtpConfig() {
    WriteJson(*ntp_persist_, ntp_config_path_);
}

void TimeServiceImpl::SaveTimeZoneConfig() {
    WriteJson(*tz_persist_, tz_config_path_);
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

    std::transform(city_list_->timezone_list.begin(), city_list_->timezone_list.end(),
                   std::back_inserter(zones), [](const auto& city) {
                       return TimeZoneItem{city.zh_cn, city.tz, static_cast<int>(city.area_id)};
                   });

    ts.timestamp  = util::GetMilliseconds();
    auto date     = util::DateTime(ts.timestamp / 1000);
    ts.timeString = date.Date().ToYMD() + " " + date.Time().ToHMS();

    ts.timeZoneId    = tz_persist_->tz_citys;
    ts.timeZoneValue = tz_persist_->value;

    return ts;
}

util::ErrorEnum TimeServiceImpl::SyncNtp(const NtpConfig& config, int time_zone_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (config.enable == 0) {
        StopNtpTimer();
        ntp_persist_->enable = 0;
        SaveNtpConfig();
    } else if (config.enable == 1) {
        ntp_persist_->enable     = 1;
        ntp_persist_->ntp_server = config.server;
        ntp_persist_->ntp_port   = config.port;
        ntp_persist_->interval   = config.interval;
        SaveNtpConfig();

        StartNtpTimer(config.interval, config.server, config.port);
    } else {
        // Test mode — single calibration attempt
        if (NtpCalibrate(config.server, config.port) == 0) {
            LOG_INFO("{}", "NTP test failed");
            return cosmo::util::ErrorEnum::TimeSyncFailed;
        }
    }

    ApplyTimeZone(time_zone_id);
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum TimeServiceImpl::SetTime(int64_t timestamp, int time_zone_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    StopNtpTimer();
    ntp_persist_->enable = 0;
    SaveNtpConfig();

    ApplyTimeZone(time_zone_id);
    auto result = cosmo::platform::SetSystemTime(timestamp);
    if (result != cosmo::util::ErrorEnum::Success) {
        LOG_WARN("set time failed:[{}], code:{}", timestamp, static_cast<int>(result));
        return result;
    }
    LOG_INFO("set time:[{}]", timestamp);
    return cosmo::util::ErrorEnum::Success;
}

}  // namespace cosmo::service
