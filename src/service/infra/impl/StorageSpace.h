// StorageSpace — Storage space management

#pragma once

#include <mutex>
#include <shared_mutex>
#include <string>

#include "util/DateTimeFormat.h"
#include "util/ErrorCode.h"

namespace cosmo {
struct StorageSpaceParam {
    uint64_t storage_reserve_space{2ULL * 1024 * 1024 * 1024};
};

class StorageSpace {
public:
    explicit StorageSpace(const std::string& record_base_path);
    ~StorageSpace() = default;

    void DoClean();

private:
    // Remove aged directories older than removed_timestamp_
    void ClearOldEventPath();
    void ClearEmptyDocument(const std::string& camera_path, const util::DateTime& dateTime,
                            bool force = false);
    void DelSpaceLmtStgy(size_t del_size);
    void IdleOperationFunction();

    std::shared_mutex mtx_;
    std::string conf_file_name_{"StorageSpaceParam.json"};
    std::string record_base_path_;
    size_t index_{0};
    int64_t removed_timestamp_{0};  // Timestamp used for aging out events
    StorageSpaceParam config_;
};
}  // namespace cosmo
