#pragma once

#include "nn/core/blob.h"
#include "nn/utils/blob_memory_size_info.h"

namespace cosmo::nn {

BlobMemorySizeInfo Calculate1DMemorySize(BlobDesc& desc);

// BlobMemorySizeInfo Calculate2DImageMemorySize(BlobDesc& desc);

}  // namespace cosmo::nn
