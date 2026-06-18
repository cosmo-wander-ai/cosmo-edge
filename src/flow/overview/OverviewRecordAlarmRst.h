#pragma once

#include <atomic>
#include <deque>
#include <list>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "flow/common/AlgDataRecord.h"
#include "flow/common/AlgDataUnit.h"
#include "util/MsgBaseTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"

namespace cosmo {

class OverviewRecordAlarmRst {
public:
    OverviewRecordAlarmRst(const std::string& task_id, const std::string& name, size_t limit_count = 500,
                           size_t limit_duration_ms = 30000);
    ~OverviewRecordAlarmRst() = default;

    void OverviewRecordFrame(const MsgRecAlarm& rec_data);
    [[nodiscard]] MsgOverviewMem GetOverviewInfo(int64_t stream_index = -1, int64_t from = -1,
                                                 int64_t to = -1);

private:
    [[nodiscard]] MsgOverviewMem GetOverviewInfoMsg();
    [[nodiscard]] MsgOverviewMem GetOverviewInfoDuration(int64_t stream_index, int64_t from, int64_t to);
    void SaveLocalFile(const MsgRecAlarm& rec_data, bool need_clear = false) const;
    void SaveMem(const MsgRecAlarm& rec_data, bool need_clear = false);

    std::shared_mutex mtx_;
    std::string task_id_;
    std::string name_;
    std::string local_file_name_;
    size_t limit_count_{kOverviewLimitCount};           // Calculated at 50fps, save up to 30 seconds 50x30
    size_t limit_duration_ms_{kOverviewLimitDuration};  // Save at most 30 seconds of data
    std::atomic<bool> is_overview_enabled_{false};

    std::deque<MsgRecAlarm> frames_;
};

using OverviewRecordAlarmRstPtr = std::shared_ptr<OverviewRecordAlarmRst>;

}  // namespace cosmo
