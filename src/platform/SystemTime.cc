// SystemTime — System time

#include "platform/SystemTime.h"

#include <sys/time.h>

#include <cerrno>
#include <cstring>

#include "util/Exec.h"
#include "util/Log.h"

namespace cosmo::platform {
cosmo::util::ErrorEnum SetSystemTime(int64_t timestamp_ms) {
#ifndef COSMO_NN_USE_SOPHON_BACKEND
    LOG_WARN("Setting system time is disabled on x86 platform. Target timestamp: {}", timestamp_ms);
    return cosmo::util::ErrorEnum::OperationNotSupport;
#else
    timeval old_time{}, new_time{};

    new_time.tv_sec  = static_cast<time_t>(timestamp_ms / 1000);
    new_time.tv_usec = static_cast<suseconds_t>((timestamp_ms % 1000) * 1000);

    gettimeofday(&old_time, nullptr);
    if (settimeofday(&new_time, nullptr) != 0) {
        LOG_ERRO("settimeofday failed for timestamp {}: {}", timestamp_ms, std::strerror(errno));
        return cosmo::util::ErrorEnum::SysErr;
    }

    std::string out;
    int ret = cosmo::util::Exec("hwclock -wu", out);
    if (ret != 0) {
        LOG_WARN("hwclock -wu failed ({}): {}", ret, out);
    }
    return cosmo::util::ErrorEnum::Success;
#endif
}
}  // namespace cosmo::platform
