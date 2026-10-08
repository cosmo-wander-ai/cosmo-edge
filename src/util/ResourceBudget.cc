#include "util/ResourceBudget.h"

#include <sys/statvfs.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <mutex>
#include <unordered_map>

#include "util/Log.h"

namespace cosmo::util {
namespace {

    std::uint64_t SaturatingMultiply(std::uint64_t lhs, std::uint64_t rhs) {
        if (lhs == 0 || rhs == 0) {
            return 0;
        }
        const auto max_value = std::numeric_limits<std::uint64_t>::max();
        return lhs > max_value / rhs ? max_value : lhs * rhs;
    }

    std::uint64_t FilesystemBytes(fsblkcnt_t blocks, unsigned long fragment_size) {
        return SaturatingMultiply(static_cast<std::uint64_t>(blocks),
                                  static_cast<std::uint64_t>(fragment_size));
    }

}  // namespace

StorageResourceBudget InspectStorageResourceBudget(const std::string& path, std::uint64_t reserve_bytes,
                                                   std::uint32_t reserve_percent) {
    StorageResourceBudget result;
    if (path.empty() || reserve_percent > 100) {
        return result;
    }

    struct statvfs status {};
    if (statvfs(path.c_str(), &status) != 0) {
        return result;
    }
    const auto fragment_size = status.f_frsize != 0 ? status.f_frsize : status.f_bsize;
    if (fragment_size == 0) {
        return result;
    }

    result.capacity_bytes         = FilesystemBytes(status.f_blocks, fragment_size);
    result.available_bytes        = FilesystemBytes(status.f_bavail, fragment_size);
    const auto percentage_reserve = SaturatingMultiply(result.capacity_bytes, reserve_percent) / 100;
    result.reserve_bytes          = std::max(reserve_bytes, percentage_reserve);
    result.usable_bytes =
        result.available_bytes > result.reserve_bytes ? result.available_bytes - result.reserve_bytes : 0;
    result.valid = true;
    return result;
}

std::uint64_t UsableStorageBytesAfterReclaim(const StorageResourceBudget& budget,
                                             std::uint64_t reclaimable_bytes) {
    if (!budget.valid) {
        return 0;
    }
    const auto max_value = std::numeric_limits<std::uint64_t>::max();
    return reclaimable_bytes > max_value - budget.usable_bytes ? max_value
                                                               : budget.usable_bytes + reclaimable_bytes;
}

namespace {
    std::mutex event_media_mutex;
    std::unordered_map<std::string, EventMediaAdmission> event_media_admission;
}  // namespace

// Reject unknown capacity and oversized writes without consuming the emergency reserve.
bool EventMediaAdmission::Admit(const StorageResourceBudget& budget, std::uint64_t bytes) {
    if (!budget.valid || budget.available_bytes < kEventMediaReserveBytes) {
        paused_ = true;
        return false;
    }
    if (paused_ && budget.available_bytes < kEventMediaResumeBytes) {
        return false;
    }
    paused_ = false;
    return bytes <= budget.available_bytes - kEventMediaReserveBytes;
}

// Share hysteresis across all alarm producers and recording writers on the event root.
bool AdmitEventMediaWrite(const std::string& root, std::uint64_t bytes) {
    std::lock_guard<std::mutex> lock(event_media_mutex);
    return event_media_admission[root].Admit(InspectStorageResourceBudget(root, kEventMediaReserveBytes),
                                             bytes);
}

// Hold the shared writer lock through close so concurrent images cannot spend the same budget.
bool WriteEventMediaFile(const std::string& root, const std::string& file, const std::uint8_t* data,
                         int size) {
    if (!data || size <= 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(event_media_mutex);
    if (!event_media_admission[root].Admit(InspectStorageResourceBudget(root, kEventMediaReserveBytes),
                                           static_cast<std::uint64_t>(size))) {
        LOG_WARN("Event media write rejected by storage reserve");
        return false;
    }
    // Keep close-error handling local to event media; generic file callers retain their behavior.
    FILE* output = std::fopen(file.c_str(), "wb");
    if (!output) {
        LOG_WARN("Event media file open failed");
        return false;
    }
    const bool written =
        std::fwrite(data, 1, static_cast<std::size_t>(size), output) == static_cast<std::size_t>(size);
    const bool closed = std::fclose(output) == 0;
    if (written && closed) {
        return true;
    }
    std::error_code ec;
    if (std::filesystem::is_regular_file(std::filesystem::symlink_status(file, ec)))
        std::filesystem::remove(file, ec);
    LOG_WARN("Event media write failed; partial file cleanup: {}", ec.message());
    return false;
}

// Route metadata through the same reserve and close-result checks as event images.
bool WriteEventMediaFile(const std::string& root, const std::string& file, const std::string& data) {
    if (data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    return WriteEventMediaFile(root, file, reinterpret_cast<const std::uint8_t*>(data.data()),
                               static_cast<int>(data.size()));
}

}  // namespace cosmo::util
