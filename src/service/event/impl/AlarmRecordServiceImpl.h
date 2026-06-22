// Alarm record service — manages task event, face event, and
// passenger flow records in the local SQLite database.

#pragma once

#include <memory>
#include <mutex>
#include <shared_mutex>

#include "db/FaceTaskEventDao.h"
#include "db/PassengerFlowDao.h"
#include "db/TaskEventDao.h"
#include "service/event/IAlarmRecordService.h"

namespace cosmo::service {

class AlarmRecordServiceImpl : public IAlarmRecordService {
public:
    AlarmRecordServiceImpl();
    ~AlarmRecordServiceImpl() override;

    std::vector<cosmo::MsgEventUnit> QueryEvents(cosmo::MsgConditionEvent& condition,
                                                 int64_t& total) override;

    AlarmQueryResult QueryAlarmRecords(const AlarmQueryCondition& condition, int order) override;

    FlowQueryResult QueryPassengerFlow(const FlowQueryCondition& condition) override;

    bool UpdateAlarmReportStatus(const std::string& record_id, bool reported) override;

    bool Insert(cosmo::AlarmRecordUnit& unit) override;

    bool InsertFace(cosmo::AlarmRecordUnit& unit) override;

    std::vector<cosmo::MsgEventUnit> QueryFace(cosmo::MsgConditionEvent& condition, int64_t& total) override;

    void RemoveItems(const std::vector<std::string>& list) override;

    bool InsertPassFlow(const std::string& camera_id, const std::string& algorithm_id, uint64_t hour,
                        int enter_num, int leave_num) override;

private:
    cosmo::db::TaskEventData AlarmDataToEventData(cosmo::AlarmRecordUnit& unit);

    cosmo::db::FaceTaskEventData AlarmDataToFaceEventData(cosmo::AlarmRecordUnit& unit);

    std::shared_mutex mtx_;
    std::shared_ptr<cosmo::db::TaskEventDao> db_event_{nullptr};
    std::shared_ptr<cosmo::db::PassengerFlowDao> db_pass_flow_event_{nullptr};
    std::shared_ptr<cosmo::db::FaceTaskEventDao> db_face_event_{nullptr};
};

}  // namespace cosmo::service
