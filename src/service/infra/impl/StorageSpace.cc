// StorageSpace — StorageSpace — Storage space management

#include "service/infra/impl/StorageSpace.h"

#include <algorithm>
#include <filesystem>

#include "service/detail/ServiceRegistry.h"
#include "service/event/IAlarmRecordService.h"
#include "util/DateTimeFormat.h"
#include "util/FormatString.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/SafeParse.h"

namespace fs = std::filesystem;

namespace cosmo {

namespace {
    constexpr int64_t kSecondsPerDay            = 86400;
    constexpr size_t kEstimatedAlarmSize        = 2 * 1024 * 1024;   // ~2MB per alarm
    constexpr size_t kAvailableSpaceMargin      = 10 * 1024 * 1024;  // 10MB
    constexpr size_t kLogIntervalCycles         = 360;               // Log every ~30 minutes
    constexpr size_t kIdleCleanupIntervalCycles = 100;               // Run every ~10 minutes
}  // namespace

StorageSpace::StorageSpace(const std::string& record_base_path) : record_base_path_(record_base_path) {}

void StorageSpace::ClearOldEventPath() {
    auto need_old_timestamp = removed_timestamp_ - kSecondsPerDay;
    if (need_old_timestamp <= 0) {
        return;
    }
    auto path      = cosmo::path::GetEventRootPath();
    auto date_time = util::GetDateTime(need_old_timestamp);
    LOG_INFO("Old To : {}-{}-{}", date_time.Date().Year(), date_time.Date().Month(), date_time.Date().Day());
    int old_date = date_time.Date().Year() * 10000 + date_time.Date().Month() * 100 + date_time.Date().Day();
    std::error_code err;
    for (auto& year : fs::directory_iterator(path, err)) {
        int year_num = util::ParseInt(year.path().filename().string());
        for (auto& mounth : fs::directory_iterator(year, err)) {
            int month_num = util::ParseInt(mounth.path().filename().string());
            for (auto& day : fs::directory_iterator(mounth, err)) {
                int day_num = util::ParseInt(day.path().filename().string());
                int date    = year_num * 10000 + month_num * 100 + day_num;
                if (date < old_date) {
                    LOG_INFO("date:{} Rmv", date);
                    fs::remove_all(day, err);
                }
            }
        }
    }
}

// src_dir = /userdata/record/camera or disk path
void StorageSpace::ClearEmptyDocument(const std::string& camera_path, const util::DateTime& date_time,
                                      bool force) {
    // Subdirectory: /userdata/record/camera/2022/06/06
    std::error_code err;

    int today = date_time.Date().Year() * 10000 + date_time.Date().Month() * 100 + date_time.Date().Day();

    for (auto& year : fs::directory_iterator(camera_path, err)) {
        int year_num    = util::ParseInt(year.path().filename().string());
        int month_count = 0;
        for (auto& mounth : fs::directory_iterator(year, err)) {
            month_count++;
            int month_num = util::ParseInt(mounth.path().filename().string());
            int day_count = 0;
            for (auto& day : fs::directory_iterator(mounth, err)) {
                day_count++;
                int day_num = util::ParseInt(day.path().filename().string());
                int date    = year_num * 10000 + month_num * 100 + day_num;
                if (date == today)  // Skip today's data
                {
                    continue;
                }
                int file_count = 0;
                // Force mode: delete the entire day directory
                if (!force) {
                    for (auto& item : fs::directory_iterator(day, err)) {
                        if (fs::is_directory(item, err))  // Filter . and ..
                        {
                            continue;
                        }
                        // Temporary file
                        std::string filename = item.path().filename().string();  // Get filename
                        if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".tmp") {
                            // Skip files ending with ".tmp"
                            continue;
                        }

                        file_count++;
                    }
                }
                if (0 == file_count) {
                    LOG_INFO("delete empty day file:{} ", day.path());
                    fs::remove_all(day, err);
                }
            }
            if (0 == day_count) {
                auto year_month       = year_num * 10000 + month_num * 100;
                auto today_year_month = date_time.Date().Year() * 10000 + date_time.Date().Month() * 100;
                if (year_month == today_year_month)  // Skip current month's files
                {
                    continue;
                } else {
                    LOG_INFO("delete empty mounth file:{} ", mounth.path());
                    fs::remove_all(mounth, err);
                }
            }
        }
        if (0 == month_count) {
            if (year_num == date_time.Date().Year())  // Skip current year's files
            {
                continue;
            } else {
                LOG_INFO("delete empty year file:{} ", year.path());
                fs::remove_all(year, err);
            }
        }
    }
}

void StorageSpace::IdleOperationFunction() {
    auto date_time = util::GetCurrentDateTime();
    // Run empty-folder cleanup during midnight hours
    if ((date_time.Time().Hour() < 1) || (date_time.Time().Hour() > 5))
        return;

    // Event path: non-force — don't delete if directory contains files
    ClearEmptyDocument(cosmo::path::GetEventRootPath(), date_time);
    // Web root path: force delete — remove even if directory contains files
    ClearEmptyDocument(cosmo::path::GetWebRootPath(), date_time, true);

    ClearOldEventPath();
}

// Space deletion strategy — remove a certain amount of data
void StorageSpace::DelSpaceLmtStgy(size_t del_size) {
    size_t alarm_unit_size = kEstimatedAlarmSize;
    service::AlarmQueryCondition queryEvent{};
    queryEvent.pageNum           = 1;
    queryEvent.pageSize          = del_size / alarm_unit_size + 1;
    queryEvent.bExportTotalCount = false;
    LOG_INFO("Del {}-{}", queryEvent.pageNum, queryEvent.pageSize);
    // Query in order — delete oldest first
    auto event_rsts =
        service::ServiceRegistry::Instance().Get<service::IAlarmRecordService>().QueryAlarmRecords(queryEvent,
                                                                                                   1);
    if (event_rsts.behaviorList.empty()) {
        LOG_INFO("{}", "Need Old Event, But Event is Empty.");
        return;
    }

    // Delete database records
    std::vector<std::string> idGroup;
    std::for_each(event_rsts.behaviorList.begin(), event_rsts.behaviorList.end(),
                  [&idGroup](const service::AlarmEventRecord& item) { idGroup.push_back(item.id); });
    service::ServiceRegistry::Instance().Get<service::IAlarmRecordService>().RemoveItems(idGroup);
    std::error_code ec;
    // Delete alarm files
    for (auto& event : event_rsts.behaviorList) {
        removed_timestamp_ = event.timestamp;
        std::vector<std::string> extraFiles;
        (void)util::DecodeJson(event.extraFiles, extraFiles);
        auto path = cosmo::path::GetEventPath(event.timestamp, false);
        for (auto file : extraFiles) {
            auto file_name = (fs::path(path) / (event.id + file)).string();

            fs::remove(file_name, ec);
        }

        auto mp4FileName = (fs::path(path) / (event.id + "_video.mp4")).string();
        fs::remove(mp4FileName, ec);
        auto mpTmpFileName = (fs::path(path) / (event.id + "_video.tmp")).string();
        fs::remove(mpTmpFileName, ec);
        auto overviewFileName = (fs::path(path) / (event.id + "_overview.json")).string();
        fs::remove(overviewFileName, ec);
        auto jsonFileName = (fs::path(path) / (event.id + ".json")).string();
        fs::remove(jsonFileName, ec);
    }
}

void StorageSpace::DoClean() {
    // Log every 30 minutes
    if (0 == index_ % kLogIntervalCycles) {
        LOG_INFO("Space Check {}", index_);
    }

    std::error_code err;
    auto space_info = fs::space(record_base_path_, err);
    if (space_info.available + kAvailableSpaceMargin < config_.storage_reserve_space) {
        DelSpaceLmtStgy(config_.storage_reserve_space - space_info.available);
    }

    // Run approximately every 10 minutes
    if (0 == index_ % kIdleCleanupIntervalCycles) {
        IdleOperationFunction();
    }

    index_++;
}

}  // namespace cosmo
