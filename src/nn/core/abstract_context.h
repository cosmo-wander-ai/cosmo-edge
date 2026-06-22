#pragma once

#include "nn/core/status.h"

namespace cosmo::nn {

class AbstractContext {
public:
    virtual ~AbstractContext(){};

    virtual Status GetCommandQueue(void** queue) = 0;

    virtual Status OnForwardBegin();

    virtual Status OnForwardEnd();

    virtual Status Synchronize() = 0;

    virtual Status SetThreadNum(int num);
};
}  // namespace cosmo::nn
