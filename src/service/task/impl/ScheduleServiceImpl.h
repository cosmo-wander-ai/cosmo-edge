// ScheduleService implementation - time template configuration management

#pragma once

#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "service/task/IScheduleService.h"
#include "service/task/dto/ScheduleMsgTypes.h"

namespace cosmo::service {

class ScheduleServiceImpl : public IScheduleService {
public:
    ScheduleServiceImpl();

    cosmo::util::ErrorEnum Add(cosmo::MsgScheduleTemplate& config, std::string& id) override;
    cosmo::util::ErrorEnum Update(cosmo::MsgScheduleTemplate& config) override;
    cosmo::util::ErrorEnum Delete(const std::string& scheduleId) override;
    std::vector<cosmo::MsgScheduleTemplate> Query(const std::string& groupId, int pageNum, int pageSize,
                                                  size_t& total) override;

    bool Exist(const std::string& scheduleId, std::string& scheduleName) override;
    bool Exist(const std::string& scheduleId) override;
    bool InRunTime(const std::string& scheduleId) override;
    std::string GetDefaultId() override;

private:
    /// Build a default schedule template covering the specified weekdays (0=Sun..6=Sat).
    static cosmo::MsgScheduleTemplate MakeDefaultSchedule(const std::string& name, const std::string& id,
                                                          int weekday_begin, int weekday_end);

    bool InRunTime(cosmo::MsgScheduleTemplate& schedule);
    bool InRunTime(const std::string& scheduleId, std::vector<cosmo::MsgScheduleTemplate>& configs);
    void SetDefault();
    bool ScheduleInDefault(const std::string& scheduleId) const;
    bool ExistDefault(const std::string& scheduleId, std::string& scheduleName) const;

    static constexpr const char* kConfFileName = "scheduleTemplate.json";
    static constexpr size_t kMaxScheduleCount  = 100;

    std::shared_mutex mtx_;
    std::vector<cosmo::MsgScheduleTemplate> config_;
    std::vector<cosmo::MsgScheduleTemplate> config_default_;
};

}  // namespace cosmo::service
