// ScheduleServiceImpl — ScheduleService implementation - time template configuration management

#include "service/task/impl/ScheduleServiceImpl.h"

#include <algorithm>
#include <filesystem>

#include "util/DateTimeFormat.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/TimeUtil.h"
#include "util/UuidUtil.h"

namespace cosmo::service {

// ── Helper ──────────────────────────────────────────────────────────────────

cosmo::MsgScheduleTemplate ScheduleServiceImpl::MakeDefaultSchedule(const std::string& name,
                                                                    const std::string& id, int weekday_begin,
                                                                    int weekday_end) {
    cosmo::MsgScheduleTemplate tpl;
    tpl.is_default   = true;
    tpl.scheduleName = name;
    tpl.scheduleId   = id;
    for (int day = weekday_begin; day < weekday_end; ++day) {
        cosmo::MsgScheduleConfig cfg;
        cfg.weekDay = day % 7;
        cosmo::MsgRunTime rt;
        rt.timeBegin = "00:00:00";
        rt.timeEnd   = "23:59:59";
        cfg.runTime.push_back(rt);
        tpl.scheduleConfig.push_back(cfg);
    }
    return tpl;
}

// ── Construction ────────────────────────────────────────────────────────────

ScheduleServiceImpl::ScheduleServiceImpl() {
    SetDefault();
    auto cfg_path = (std::filesystem::path(cosmo::path::GetCfgPath()) / kConfFileName).string();
    if (!cosmo::util::LoadStructFromJsonFile(cfg_path, config_)) {
        LOG_WARN("Failed to load schedule config from {}", cfg_path);
    }
    LOG_INFO("{}", "ScheduleServiceImpl Init");
}

void ScheduleServiceImpl::SetDefault() {
    config_default_.push_back(MakeDefaultSchedule("全天候", "e89c6c6385e5454b35cde0d1653vg", 0, 7));
    config_default_.push_back(MakeDefaultSchedule("工作日", "e89c6c6385e545e935cde0d1653vw", 1, 6));
    config_default_.push_back(MakeDefaultSchedule("周末工作", "ec04a56542d347ef993ebf46a9317", 6, 8));
}

std::string ScheduleServiceImpl::GetDefaultId() {
    return config_default_.front().scheduleId;
}

// ── CRUD ────────────────────────────────────────────────────────────────────

cosmo::util::ErrorEnum ScheduleServiceImpl::Add(cosmo::MsgScheduleTemplate& config, std::string& id) {
    config.scheduleId = cosmo::util::GenerateUUID();
    id                = config.scheduleId;

    std::lock_guard<std::shared_mutex> lock(mtx_);
    if (config_.size() >= kMaxScheduleCount) {
        return cosmo::util::ErrorEnum::TimeTemplateCountLimit;
    }
    config_.push_back(config);

    auto path = (std::filesystem::path(cosmo::path::GetCfgPath()) / kConfFileName).string();
    if (!cosmo::util::SaveStructToJsonFile(path, config_)) {
        LOG_WARN("Failed to save schedule config to {}", path);
    }
    return cosmo::util::ErrorEnum::Success;
}

bool ScheduleServiceImpl::ScheduleInDefault(const std::string& scheduleId) const {
    return std::any_of(config_default_.begin(), config_default_.end(),
                       [&](const cosmo::MsgScheduleTemplate& cfg) { return cfg.scheduleId == scheduleId; });
}

cosmo::util::ErrorEnum ScheduleServiceImpl::Update(cosmo::MsgScheduleTemplate& config) {
    std::lock_guard<std::shared_mutex> lock(mtx_);

    if (ScheduleInDefault(config.scheduleId)) {
        return cosmo::util::ErrorEnum::DefaultCantBeUpdate;
    }
    auto it = std::find_if(config_.begin(), config_.end(), [&](const cosmo::MsgScheduleTemplate& cfg) {
        return cfg.scheduleId == config.scheduleId;
    });

    if (it == config_.end()) {
        LOG_INFO("{} Not Exist", config.scheduleId);
        return cosmo::util::ErrorEnum::TimeTemplateNotExist;
    }
    *it = config;
    LOG_INFO("{}/{} Update", config.scheduleId, config.scheduleName);

    auto path = (std::filesystem::path(cosmo::path::GetCfgPath()) / kConfFileName).string();
    if (!cosmo::util::SaveStructToJsonFile(path, config_)) {
        LOG_WARN("Failed to save schedule config to {}", path);
    }
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum ScheduleServiceImpl::Delete(const std::string& scheduleId) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    if (ScheduleInDefault(scheduleId)) {
        return cosmo::util::ErrorEnum::DefaultCantBeDelete;
    }
    auto it = std::find_if(config_.begin(), config_.end(), [&](const cosmo::MsgScheduleTemplate& cfg) {
        return cfg.scheduleId == scheduleId;
    });

    if (it == config_.end()) {
        LOG_INFO("{} Not Exist", scheduleId);
        return cosmo::util::ErrorEnum::TimeTemplateNotExist;
    }
    LOG_INFO("{}/{} Delete", it->scheduleId, it->scheduleName);
    config_.erase(it);

    auto path = (std::filesystem::path(cosmo::path::GetCfgPath()) / kConfFileName).string();
    if (!cosmo::util::SaveStructToJsonFile(path, config_)) {
        LOG_WARN("Failed to save schedule config to {}", path);
    }
    return cosmo::util::ErrorEnum::Success;
}

// ── Query ───────────────────────────────────────────────────────────────────

bool ScheduleServiceImpl::Exist(const std::string& scheduleId, std::string& schedule_name) {
    if (ExistDefault(scheduleId, schedule_name)) {
        return true;
    }

    std::shared_lock<std::shared_mutex> lock(mtx_);
    auto it = std::find_if(config_.begin(), config_.end(), [&](const cosmo::MsgScheduleTemplate& cfg) {
        return cfg.scheduleId == scheduleId;
    });

    if (it == config_.end()) {
        return false;
    }
    schedule_name = it->scheduleName;
    return true;
}

bool ScheduleServiceImpl::ExistDefault(const std::string& scheduleId, std::string& schedule_name) const {
    // config_default_ is immutable after construction, no lock needed
    auto it =
        std::find_if(config_default_.begin(), config_default_.end(),
                     [&](const cosmo::MsgScheduleTemplate& cfg) { return cfg.scheduleId == scheduleId; });

    if (it == config_default_.end()) {
        return false;
    }
    schedule_name = it->scheduleName;
    return true;
}

bool ScheduleServiceImpl::Exist(const std::string& scheduleId) {
    std::string schedule_name;
    if (ExistDefault(scheduleId, schedule_name)) {
        return true;
    }
    return Exist(scheduleId, schedule_name);
}

// ── InRunTime ───────────────────────────────────────────────────────────────

bool ScheduleServiceImpl::InRunTime(cosmo::MsgScheduleTemplate& schedule) {
    auto date_time = cosmo::util::GetCurrentDateTime();
    for (auto& unit : schedule.scheduleConfig) {
        if (unit.weekDay != date_time.Date().WeekDay()) {
            continue;
        }
        for (auto& runtime : unit.runTime) {
            auto time_begin    = cosmo::util::HMSTime(runtime.timeBegin).ToInt();
            auto time_end      = cosmo::util::HMSTime(runtime.timeEnd).ToInt();
            auto date_time_sec = date_time.Time().ToInt();
            if ((time_begin <= date_time_sec) && (date_time_sec <= time_end)) {
                return true;
            }
        }
    }
    return false;
}

bool ScheduleServiceImpl::InRunTime(const std::string& scheduleId,
                                    std::vector<cosmo::MsgScheduleTemplate>& configs) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    auto it = std::find_if(configs.begin(), configs.end(), [&](const cosmo::MsgScheduleTemplate& cfg) {
        return cfg.scheduleId == scheduleId;
    });

    if (it == configs.end()) {
        return false;
    }
    return InRunTime(*it);
}

bool ScheduleServiceImpl::InRunTime(const std::string& scheduleId) {
    if (InRunTime(scheduleId, config_default_)) {
        return true;
    }

    return InRunTime(scheduleId, config_);
}

// ── Pagination ──────────────────────────────────────────────────────────────

std::vector<cosmo::MsgScheduleTemplate> ScheduleServiceImpl::Query(const std::string& group_id, int page_num,
                                                                   int page_size, size_t& total) {
    std::vector<cosmo::MsgScheduleTemplate> infos;
    if ((page_num < 1) || (page_size < 1)) {
        return infos;
    }
    std::shared_lock<std::shared_mutex> lock(mtx_);
    size_t index       = 0;
    size_t index_start = (page_num - 1) * page_size;
    size_t index_end   = page_num * page_size;
    total              = config_.size() + config_default_.size();

    // Collect matching records from both default and user-defined configs
    auto collect = [&](const std::vector<cosmo::MsgScheduleTemplate>& source) {
        for (const auto& info : source) {
            if (!group_id.empty() && group_id != info.groupId) {
                continue;
            }
            ++index;
            if (index > index_start && index <= index_end) {
                infos.push_back(info);
            } else if (index > index_end) {
                break;
            }
        }
    };

    collect(config_default_);
    collect(config_);

    return infos;
}

}  // namespace cosmo::service
