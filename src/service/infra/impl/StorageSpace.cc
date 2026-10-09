// StorageSpace — StorageSpace — Storage space management

#include "service/infra/impl/StorageSpace.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <set>

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
    constexpr size_t kEstimatedAlarmSize        = 2 * 1024 * 1024;
    constexpr size_t kMaxCleanupBatch           = 256;
    constexpr auto kOrphanGracePeriod           = std::chrono::hours(1);
    constexpr size_t kLogIntervalCycles         = 360;  // Log every ~30 minutes
    constexpr size_t kIdleCleanupIntervalCycles = 100;  // Run every ~10 minutes
}  // namespace

// Construct a cleaner for one data volume; explicit policy supports isolated validation.
StorageSpace::StorageSpace(const std::string& record_base_path, StorageSpaceParam config)
    : record_base_path_(record_base_path), config_(config) {}

// Bound database work per pass and keep recent files and active recording groups intact.
void StorageSpace::ClearOrphanFiles() {
    std::error_code ec;
    std::set<fs::path> candidates;
    const auto cutoff = fs::file_time_type::clock::now() - kOrphanGracePeriod;
    fs::recursive_directory_iterator it(record_base_path_, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_symlink(ec) || !it->is_regular_file(ec)) {
            ec.clear();
            continue;
        }
        const auto file = it->path();
        if (file.string() <= orphan_scan_cursor_) {
            continue;
        }
        const auto name = file.filename().string();
        if (name.size() < 41) {
            continue;
        }
        const auto id = name.substr(0, 36);
        bool valid_id = true;
        for (size_t i = 0; i < id.size(); ++i) {
            const bool separator = i == 8 || i == 13 || i == 18 || i == 23;
            if (separator ? id[i] != '-' : !std::isxdigit(static_cast<unsigned char>(id[i]))) {
                valid_id = false;
                break;
            }
        }
        const auto suffix = name.substr(36);
        if (!valid_id || (suffix != "_full.jpg" && suffix != "_orig.jpg" && suffix != "_detect.jpg" &&
                          suffix != "_video.mp4" && suffix != "_overview.json" && suffix != ".json" &&
                          suffix != "_full-best.jpg" && suffix != "_orig-best.jpg" &&
                          suffix != "_detect-best.jpg" && suffix != "_base.jpg")) {
            continue;
        }
        const auto modified = fs::last_write_time(file, ec);
        if (ec || modified > cutoff) {
            ec.clear();
            continue;
        }
        candidates.insert(file);
        if (candidates.size() > kMaxCleanupBatch) {
            candidates.erase(std::prev(candidates.end()));
        }
    }
    if (ec) {
        LOG_WARN("Orphan scan failed: {}", ec.message());
        return;
    }
    if (candidates.empty()) {
        orphan_scan_cursor_.clear();
        return;
    }
    auto& records      = service::ServiceRegistry::Instance().Get<service::IAlarmRecordService>();
    uint64_t reclaimed = 0;
    for (const auto& file : candidates) {
        orphan_scan_cursor_ = file.string();
        const auto id       = file.filename().string().substr(0, 36);
        if (records.HasStoredEvent(id)) {
            continue;
        }
        const bool recording = fs::exists(file.parent_path() / (id + "_video.tmp"), ec);
        if (ec || recording) {
            ec.clear();
            continue;
        }
        const auto size = fs::file_size(file, ec);
        if (ec) {
            ec.clear();
            continue;
        }
        if (fs::remove(file, ec)) {
            reclaimed += size;
            const auto remaining = fs::space(record_base_path_, ec);
            if (!ec && remaining.available >= config_.storage_reserve_space) {
                break;
            }
            ec.clear();
        } else if (ec) {
            LOG_WARN("Orphan removal failed: {}", ec.message());
            ec.clear();
        }
    }
    LOG_INFO("Orphan cleanup: checked={} reclaimedBytes={}", candidates.size(), reclaimed);
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
                        // Nested directories and temporary recordings also make this day nonempty.
                        (void)item;
                        file_count++;
                    }
                }
                if (0 == file_count) {
                    LOG_INFO("delete empty day file:{} ", day.path());
                    if (force) {
                        fs::remove_all(day, err);
                    } else {
                        fs::remove(day, err);
                    }
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
                    fs::remove(mounth, err);
                }
            }
        }
        if (0 == month_count) {
            if (year_num == date_time.Date().Year())  // Skip current year's files
            {
                continue;
            } else {
                LOG_INFO("delete empty year file:{} ", year.path());
                fs::remove(year, err);
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
}

// Remove files before their index so full-disk database writes cannot block reclamation.
void StorageSpace::DelSpaceLmtStgy(size_t del_size) {
    service::AlarmQueryCondition query{};
    query.pageNum  = 1;
    query.pageSize = static_cast<int>(std::min(kMaxCleanupBatch, del_size / kEstimatedAlarmSize + 1));
    query.bExportTotalCount = false;
    auto& records           = service::ServiceRegistry::Instance().Get<service::IAlarmRecordService>();
    const auto events       = records.QueryAlarmRecords(query, 1);
    uint64_t reclaimed      = 0;
    for (const auto& event : events.behaviorList) {
        if (event.id.empty() || fs::path(event.id).filename().string() != event.id) {
            continue;
        }
        std::vector<std::string> suffixes;
        if (!util::DecodeJson(event.extraFiles, suffixes)) {
            LOG_WARN("Storage cleanup skipped invalid event file index");
            continue;
        }
        suffixes.insert(suffixes.end(), {"_video.mp4", "_overview.json", ".json"});
        const auto directory = cosmo::path::GetEventPath(event.timestamp, false);
        std::error_code ec;
        if (fs::exists(fs::path(directory) / (event.id + "_video.tmp"), ec) || ec) {
            continue;
        }
        bool removed = true;
        for (const auto& suffix : suffixes) {
            if (suffix.empty() || (suffix.front() != '_' && suffix.front() != '.') ||
                suffix.find('/') != std::string::npos || suffix.find('\\') != std::string::npos) {
                removed = false;
                break;
            }
            const auto file = fs::path(directory) / (event.id + suffix);
            const auto size = fs::file_size(file, ec);
            ec.clear();
            if (fs::remove(file, ec)) {
                if (size != static_cast<uintmax_t>(-1))
                    reclaimed += size;
            } else if (ec) {
                LOG_WARN("Indexed event removal failed: {}", ec.message());
                removed = false;
            }
        }
        if (removed)
            records.RemoveItems({event.id});
        if (reclaimed >= del_size)
            break;
    }
    LOG_INFO("Indexed cleanup: reclaimedBytes={}", reclaimed);
}

// Recheck measured space and isolate failures so a bad database cannot kill periodic cleanup.
void StorageSpace::DoClean() {
    if (0 == index_ % kLogIntervalCycles)
        LOG_INFO("Space Check {}", index_);
    std::error_code ec;
    const auto before = fs::space(record_base_path_, ec);
    if (ec) {
        LOG_WARN("Storage space check failed: {}", ec.message());
        return;
    }
    if (before.available < config_.storage_reserve_space) {
        try {
            DelSpaceLmtStgy(config_.storage_reserve_space - before.available);
        } catch (const std::exception& error) {
            LOG_WARN("Indexed cleanup failed: {}", error.what());
        }
        try {
            const auto remaining = fs::space(record_base_path_, ec);
            if (!ec && remaining.available < config_.storage_reserve_space) {
                ClearOrphanFiles();
            }
        } catch (const std::exception& error) {
            LOG_WARN("Orphan cleanup failed: {}", error.what());
        }
        const auto after = fs::space(record_base_path_, ec);
        if (!ec)
            LOG_INFO("Storage cleanup: availableBefore={} availableAfter={} reserve={}", before.available,
                     after.available, config_.storage_reserve_space);
    }
    if (0 == index_ % kIdleCleanupIntervalCycles) {
        try {
            IdleOperationFunction();
        } catch (const std::exception& error) {
            LOG_WARN("Idle cleanup failed: {}", error.what());
        }
    }
    index_++;
}

}  // namespace cosmo
