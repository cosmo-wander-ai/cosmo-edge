// Memory pool status DTO — extracted from mem/MemoryPoolMng.h
// so that service interfaces don't depend on mem/ headers.

#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cosmo::service {

struct BlockStatusDto {
    pid_t thread_id{0};
    int64_t duration{0};
    int64_t malloc_timepoint{0};
};

struct PoolStatusDto {
    size_t pool_size{0};
    size_t idle_cnt{0};
    size_t used_cnt{0};
    std::vector<BlockStatusDto> used_nodes_status;
};

}  // namespace cosmo::service
