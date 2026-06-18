/// @file IAlarmRecordService.h
/// @brief Alarm and event record service interface — provides CRUD operations
///        for alarm records, face event records, and passenger flow statistics.
#pragma once

#include <string>
#include <system_error>
#include <vector>

#include "service/event/dto/AlarmRecordTypes.h"
#include "util/MsgBaseTypes.h"
#include "util/dto/EventMsgTypes.h"

namespace cosmo::service {

/// Manages alarm event records, face recognition event records, and
/// passenger flow statistics in the local database.
///
/// Consumed by the API layer for event queries and by the flow layer's
/// alarm pipeline for event insertion.
class IAlarmRecordService {
public:
    virtual ~IAlarmRecordService() = default;

    /// Query alarm events with pagination and filtering.
    /// @param condition Filter and pagination parameters (modified in-place for defaults).
    /// @param total     [out] Total matching record count.
    /// @return Vector of matching event records.
    virtual std::vector<cosmo::MsgEventUnit> QueryEvents(cosmo::MsgConditionEvent& condition,
                                                         int64_t& total) = 0;

    /// Query raw alarm records with ordering options.
    /// @param condition Structured query condition.
    /// @param order     Sort order (0 = ascending, 1 = descending).
    /// @return Result set containing matched records and metadata.
    virtual AlarmQueryResult QueryAlarmRecords(const AlarmQueryCondition& condition, int order) = 0;

    /// Query passenger flow statistics for a time range and camera.
    /// @param condition Passenger flow query condition.
    /// @return Result set with enter/leave counts per time bucket.
    virtual FlowQueryResult QueryPassengerFlow(const FlowQueryCondition& condition) = 0;

    /// Update the platform report status of an alarm record.
    /// @param recordId Alarm record identifier.
    /// @param reported true if the record has been reported to the platform.
    /// @return true on success.
    virtual bool UpdateAlarmReportStatus(const std::string& record_id, bool reported) = 0;

    /// Insert a new alarm event record.
    /// @param unit Alarm record data (modified in-place with generated fields).
    /// @return true on success.
    virtual bool Insert(cosmo::AlarmRecordUnit& unit) = 0;

    /// Insert a new face recognition alarm event record.
    /// @param unit Face alarm record data.
    /// @return true on success.
    virtual bool InsertFace(cosmo::AlarmRecordUnit& unit) = 0;

    /// Query face recognition event records with pagination.
    /// @param condition Filter and pagination parameters.
    /// @param total     [out] Total matching record count.
    /// @return Vector of matching face event records.
    virtual std::vector<cosmo::MsgEventUnit> QueryFace(cosmo::MsgConditionEvent& condition,
                                                       int64_t& total) = 0;

    /// Remove alarm records by ID list.
    /// @param list Record IDs to delete.
    virtual void RemoveItems(const std::vector<std::string>& list) = 0;

    /// Insert a passenger flow data point.
    /// @param cameraId   Camera identifier.
    /// @param algorithmId Algorithm identifier.
    /// @param hour        Hour timestamp (epoch-aligned).
    /// @param enterNum    Enter count for this hour.
    /// @param leaveNum    Leave count for this hour.
    /// @return true on success.
    virtual bool InsertPassFlow(const std::string& camera_id, const std::string& algorithm_id, uint64_t hour,
                                int enter_num, int leave_num) = 0;
};

}  // namespace cosmo::service
