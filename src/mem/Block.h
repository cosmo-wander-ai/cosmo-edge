#pragma once

#include <unistd.h>

#include <cstdint>

namespace cosmo::mem {

struct BlockStatus {
    pid_t thread_id          = 0;
    int64_t duration         = 0;
    int64_t malloc_timepoint = 0;
};

struct Block {
    size_t size   = 0;
    uint8_t* data = nullptr;

    BlockStatus status;
};

}  // namespace cosmo::mem
