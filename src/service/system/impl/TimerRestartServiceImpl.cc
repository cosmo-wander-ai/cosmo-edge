// TimerRestartServiceImpl — Timer Restart Service Impl implementation.

#include "service/system/impl/TimerRestartServiceImpl.h"

#include <sys/reboot.h>
#include <unistd.h>

#include <thread>

#include "service/detail/ServiceRegistry.h"
#include "service/system/IConfigReadService.h"
#include "service/system/ISystemOperationService.h"
#include "util/DateTimeFormat.h"
#include "util/FormatString.h"
#include "util/Log.h"

namespace cosmo::service {

TimerRestartServiceImpl::~TimerRestartServiceImpl() {
    if (restart_task_id_ != cosmo::kInvalidTaskId) {
        timer_->Cancel(restart_task_id_);
        restart_task_id_ = cosmo::kInvalidTaskId;
    }
    if (timer_) {
        timer_->Destroy();
    }

    LOG_INFO("{}", "TimerRestartServiceImpl Delete");
}

void TimerRestartServiceImpl::Start() {
    timer_ = std::make_unique<cosmo::PeriodicTimer>("TimetRestart");
    timer_->Start();
    restart_task_id_ = timer_->Schedule([this]() { CheckRestart(); }, 10000);
    LOG_INFO("{}", "Timer Restart Started");
}

void TimerRestartServiceImpl::CheckRestart() {
    cosmo::CfgRebootParamInfo param_info =
        ServiceRegistry::Instance().Get<IConfigReadService>().GetRebootParam();
    auto is_timing_restart = param_info.isTimingRestart;
    if (!is_timing_restart) {
        return;
    }
    // 0: every day  1-7: Monday to Sunday
    auto week_day                   = param_info.weekDay;
    cosmo::util::DateTime date_time = cosmo::util::GetCurrentDateTime();
    if (0 == week_day) {
        // Check daily time for restart
        DateSecRestart(static_cast<int64_t>(date_time.Time().Hour()) * 3600 +
                       static_cast<int64_t>(date_time.Time().Minute()) * 60 +
                       static_cast<int64_t>(date_time.Time().Second()));
        return;
    } else {
        if (7 == week_day)  // Convert Sunday to date-based Sunday
        {
            week_day = 0;
        }
    }

    // 0: Sunday  1-6: Monday to Saturday
    auto date_week_day = date_time.Date().WeekDay();
    LOG_INFO("[TimerRestart] Conf WeekDay:{} Today WeekDay:{} ....", week_day, date_week_day);
    if (week_day == date_week_day) {
        // Check daily time for restart
        DateSecRestart(static_cast<int64_t>(date_time.Time().Hour()) * 3600 +
                       static_cast<int64_t>(date_time.Time().Minute()) * 60 +
                       static_cast<int64_t>(date_time.Time().Second()));
        return;
    }
}

void TimerRestartServiceImpl::DateSecRestart(int64_t today_sec) {
    cosmo::CfgRebootParamInfo param_info =
        ServiceRegistry::Instance().Get<IConfigReadService>().GetRebootParam();
    auto restart_time = param_info.restartTimeSec;
    LOG_INFO("[TimerRestart] Conf daySec:{} Today daySec:{} ....", restart_time, today_sec);
    if ((today_sec > restart_time) && (today_sec < restart_time + 30)) {
        // restart
        LOG_INFO("{}", "[TimerRestart] Restart....");
        ServiceRegistry::Instance().Get<ISystemOperationService>().RebootDevice("TimerRestart");
    }
}
}  // namespace cosmo::service
