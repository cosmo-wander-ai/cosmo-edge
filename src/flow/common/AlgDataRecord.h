#pragma once

#include "flow/common/AlgDataUnit.h"
#include "util/dto/CosmoFwd.h"

namespace cosmo {

struct MsgRecAlarm;

// Overview recording limits
constexpr int kOverviewLimitCount    = 250;
constexpr int kOverviewLimitDuration = 10000;  // milliseconds

void RecordAlgDataClearTaskData(const std::string& taskId);

void RecordAlgDataUrl(const std::string& taskId, const std::string& url);
void RecordAlgTaskInfo(const std::string& taskId, const MsgTaskCreateRecv& data);
void RecordAlgTaskAction(const std::string& taskId, ActionAlgPtr data);
void RecordAlgDataAlarm(const std::string& taskId, const MsgRecAlarm& data);

DataDetTrackClassifyPtr GenRandomDetBoxs();

}  // namespace cosmo
