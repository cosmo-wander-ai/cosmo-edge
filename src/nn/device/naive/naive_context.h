#pragma once

#include "nn/core/abstract_context.h"

namespace cosmo::nn {

class NaiveContext : public AbstractContext {
public:
    virtual Status GetCommandQueue(void** command_queue) override;

    virtual Status Synchronize() override;
};

}  // namespace cosmo::nn
