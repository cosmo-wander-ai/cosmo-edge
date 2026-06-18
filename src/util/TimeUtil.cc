// Time utility implementation

#include "util/TimeUtil.h"

namespace cosmo::util {

void GetDelayDate(int delayDays, time_t currentTime, uint16_t& year, uint8_t& month, uint8_t& day) {
    time_t targetTime = currentTime + static_cast<time_t>(delayDays) * 24 * 3600;

    struct tm t;
    localtime_r(&targetTime, &t);

    year  = static_cast<uint16_t>(t.tm_year + 1900);
    month = static_cast<uint8_t>(t.tm_mon + 1);
    day   = static_cast<uint8_t>(t.tm_mday);
}

}  // namespace cosmo::util
