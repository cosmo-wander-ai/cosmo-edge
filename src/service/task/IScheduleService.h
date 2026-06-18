/// @file IScheduleService.h
/// @brief Schedule service interface — CRUD operations for time-based
///        scheduling templates used by camera–algorithm task bindings.
#pragma once

#include <string>
#include <vector>

#include "service/detail/ServiceRegistry.h"
#include "service/task/dto/ScheduleMsgTypes.h"
#include "util/ErrorCode.h"

namespace cosmo::service {

/// Manages scheduling templates that define time windows during which
/// camera–algorithm tasks are active.
///
/// Each schedule template defines daily time ranges (e.g. 08:00–18:00)
/// and optional day-of-week rules.  Tasks bound to a schedule only
/// run during the defined active periods.
class IScheduleService {
public:
    virtual ~IScheduleService() = default;

    /// Add a new schedule template.
    /// @param config Schedule template configuration (modified in-place with defaults).
    /// @param id     [out] Generated schedule ID.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum Add(cosmo::MsgScheduleTemplate& config, std::string& id) = 0;

    /// Update an existing schedule template.
    /// @param config Updated schedule template configuration.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum Update(cosmo::MsgScheduleTemplate& config) = 0;

    /// Delete a schedule template by ID.
    /// @param scheduleId Schedule identifier to delete.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum Delete(const std::string& scheduleId) = 0;

    /// Query schedule templates with pagination.
    /// @param groupId  Group filter (empty for all).
    /// @param pageNum  Page number (1-based).
    /// @param pageSize Page size.
    /// @param total    [out] Total matching record count.
    /// @return Vector of matching schedule templates.
    virtual std::vector<cosmo::MsgScheduleTemplate> Query(const std::string& groupId, int pageNum,
                                                          int pageSize, size_t& total) = 0;

    /// Check whether a schedule exists and get its name.
    /// @param scheduleId   Schedule identifier.
    /// @param scheduleName [out] Schedule display name if found.
    /// @return true if the schedule exists.
    virtual bool Exist(const std::string& scheduleId, std::string& scheduleName) = 0;

    /// Check whether a schedule exists.
    /// @param scheduleId Schedule identifier.
    /// @return true if the schedule exists.
    virtual bool Exist(const std::string& scheduleId) = 0;

    /// Check whether the current time falls within a schedule's active period.
    /// @param scheduleId Schedule identifier.
    /// @return true if currently in the active time window.
    virtual bool InRunTime(const std::string& scheduleId) = 0;

    /// Get the default (always-active) schedule ID.
    /// @return Default schedule identifier.
    virtual std::string GetDefaultId() = 0;
};

}  // namespace cosmo::service
