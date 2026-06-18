#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "service/system/ITimeService.h"

namespace cosmo::service {

class TimeServiceImpl : public ITimeService {
public:
    TimeServiceImpl();
    ~TimeServiceImpl() override;

    TimeServiceImpl(const TimeServiceImpl&)            = delete;
    TimeServiceImpl& operator=(const TimeServiceImpl&) = delete;

    // ---- ITimeService ----
    TimeStatus GetTimeStatus(std::vector<TimeZoneItem>& zones) override;
    cosmo::util::ErrorEnum SyncNtp(const NtpConfig& config, int timeZoneId) override;
    cosmo::util::ErrorEnum SetTime(int64_t timestamp, int timeZoneId) override;

private:
    struct TimeZonePersist;
    struct NtpPersist;
    struct TimeZoneCityList;

    void LoadConfig();
    void ApplyTimeZone(int city_id);
    void SaveNtpConfig();
    void SaveTimeZoneConfig();

    // NTP calibration — connects to server, returns 100ns-unit timestamp (0 on failure)
    int64_t NtpCalibrate(const std::string& host, int port);

    // NTP periodic sync timer
    void StartNtpTimer(int intervalMin, const std::string& host, int port);
    void StopNtpTimer();

    mutable std::mutex mutex_;

    // NTP sync thread
    std::thread ntp_thread_;
    std::mutex ntp_mutex_;
    std::condition_variable ntp_cv_;
    bool ntp_stop_{true};

    std::unique_ptr<TimeZonePersist> tz_persist_;
    std::unique_ptr<NtpPersist> ntp_persist_;
    std::unique_ptr<TimeZoneCityList> city_list_;

    std::string tz_config_path_;
    std::string ntp_config_path_;
};

}  // namespace cosmo::service
