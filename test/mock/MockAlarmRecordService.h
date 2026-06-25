#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/event/IAlarmRecordService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockAlarmRecordService : public cosmo::service::IAlarmRecordService {
public:
    MAKE_MOCK2(QueryEvents, std::vector<cosmo::MsgEventUnit>(cosmo::MsgConditionEvent&, int64_t&), override);
    MAKE_MOCK2(QueryAlarmRecords,
               cosmo::service::AlarmQueryResult(const cosmo::service::AlarmQueryCondition&, int), override);
    MAKE_MOCK1(QueryPassengerFlow, cosmo::service::FlowQueryResult(const cosmo::service::FlowQueryCondition&),
               override);
    MAKE_MOCK2(UpdateAlarmReportStatus, bool(const std::string&, bool), override);
    MAKE_MOCK1(Insert, bool(cosmo::AlarmRecordUnit&), override);
    MAKE_MOCK1(InsertFace, bool(cosmo::AlarmRecordUnit&), override);
    MAKE_MOCK2(QueryFace, std::vector<cosmo::MsgEventUnit>(cosmo::MsgConditionEvent&, int64_t&), override);
    MAKE_MOCK1(RemoveItems, void(const std::vector<std::string>&), override);
    MAKE_MOCK5(InsertPassFlow, bool(const std::string&, const std::string&, uint64_t, int, int), override);
};

}  // namespace cosmo::test
