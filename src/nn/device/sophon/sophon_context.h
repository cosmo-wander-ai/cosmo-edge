#pragma once

#include "bmcv_api.h"
#include "bmcv_api_ext.h"
#include "bmlib_runtime.h"
#include "bmruntime_interface.h"
#include "nn/core/abstract_context.h"

namespace cosmo::nn {

class SophonContext : public AbstractContext {
public:
    ~SophonContext();

    virtual Status GetCommandQueue(void** command_queue) override;

    virtual Status Synchronize() override;

    Status Setup(int device_id_);

public:
    int device_id = 0;
};

}  // namespace cosmo::nn
