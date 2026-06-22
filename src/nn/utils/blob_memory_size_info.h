#pragma once

#include "nn/core/common.h"

namespace cosmo::nn {

struct BlobMemorySizeInfo {
    DataType data_type;
    std::vector<int> dims;
};

int64_t GetBlobMemoryBytesSize(BlobMemorySizeInfo& info);

}  // namespace cosmo::nn
