// StorageSpace — Storage space management

#pragma once

#include <cstdint>
#include <string>

#include "util/DateTimeFormat.h"

namespace cosmo {
struct StorageSpaceParam {
    uint64_t storage_reserve_space{2ULL * 1024 * 1024 * 1024};
};

class StorageSpace {
public:
    explicit StorageSpace(const std::string& record_base_path, StorageSpaceParam config = {});
    ~StorageSpace() = default;

    void DoClean();

private:
    // Reclaim only aged, recognized files absent from both event tables.
    void ClearOrphanFiles();
    void ClearEmptyDocument(const std::string& camera_path, const util::DateTime& dateTime,
                            bool force = false);
    void DelSpaceLmtStgy(size_t del_size);
    void IdleOperationFunction();

    std::string record_base_path_;
    size_t index_{0};
    std::string orphan_scan_cursor_;  // Resume bounded scans without starving later files.
    StorageSpaceParam config_;
};
}  // namespace cosmo
