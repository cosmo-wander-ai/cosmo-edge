/// @file ResourceBudget.h
/// @brief Resource-driven filesystem admission helpers.
#pragma once

#include <cstdint>
#include <string>

namespace cosmo::util {

constexpr std::uint64_t kDefaultStorageReserveBytes = 512ULL * 1024 * 1024;
// Keep transfer admission and post-upload consumers on the same emergency
// reserve policy. Event retention has its own business waterline; applying an
// additional percentage here can reduce the usable budget to zero on devices
// that still have enough space for a small operation.
constexpr std::uint32_t kDefaultStorageReservePercent = 0;

struct StorageResourceBudget {
    bool valid{false};
    std::uint64_t capacity_bytes{0};
    std::uint64_t available_bytes{0};
    std::uint64_t reserve_bytes{0};
    std::uint64_t usable_bytes{0};
};

/// Inspect the filesystem containing @p path. The usable budget is the
/// unprivileged available space minus the larger of a fixed and percentage
/// reserve. The path must already exist.
[[nodiscard]] StorageResourceBudget InspectStorageResourceBudget(
    const std::string& path, std::uint64_t reserve_bytes = kDefaultStorageReserveBytes,
    std::uint32_t reserve_percent = kDefaultStorageReservePercent);

/// Return the usable budget after including bytes that the operation will
/// reclaim before writing. The addition saturates on overflow.
[[nodiscard]] std::uint64_t UsableStorageBytesAfterReclaim(const StorageResourceBudget& budget,
                                                           std::uint64_t reclaimable_bytes);

constexpr std::uint64_t kEventMediaReserveBytes = 512ULL * 1024 * 1024;
constexpr std::uint64_t kEventMediaResumeBytes  = 2ULL * 1024 * 1024 * 1024;

/// Apply event-media hysteresis to a measured budget. Call under the writer's lock.
/// Pause below the emergency reserve and resume only at the retention waterline.
class EventMediaAdmission {
public:
    bool Admit(const StorageResourceBudget& budget, std::uint64_t bytes);

private:
    bool paused_{false};
};

/// Check admission before starting a recording or appending a sample on this data volume.
[[nodiscard]] bool AdmitEventMediaWrite(const std::string& root, std::uint64_t bytes);

/// Serialize admission and synchronous writes; remove partial files on write failure.
[[nodiscard]] bool WriteEventMediaFile(const std::string& root, const std::string& file,
                                       const std::uint8_t* data, int size);
[[nodiscard]] bool WriteEventMediaFile(const std::string& root, const std::string& file,
                                       const std::string& data);

}  // namespace cosmo::util
