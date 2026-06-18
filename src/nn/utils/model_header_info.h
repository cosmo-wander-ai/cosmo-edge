#pragma once

#include <vector>

#include "nn/core/macros.h"

namespace cosmo::nn {

static const int g_magic_num = 0xABCD0001;

struct ModelHeaderInfo {
    int magic_num = g_magic_num;

    // [char long-int long-int ...]
    // [model_num size size ...]
    char reserved[256];
};

PUBLIC std::vector<long int> GetModelSize(ModelHeaderInfo& info);

}  // namespace cosmo::nn
