#pragma once

#include "mem/Block.h"

namespace cosmo::mem {

class Allocator {
public:
    Allocator() = default;

    virtual ~Allocator() = default;

    [[nodiscard]] virtual Block* Allocate(size_t size) = 0;

    virtual void Free(Block* block) = 0;
};

}  // namespace cosmo::mem
