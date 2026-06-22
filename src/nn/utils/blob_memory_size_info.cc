#include "nn/utils/blob_memory_size_info.h"

#include "nn/utils/data_type_utils.h"
#include "nn/utils/dims_vector_utils.h"

namespace cosmo::nn {

static constexpr int64_t kMaxBlobBytes = 512L * 1024 * 1024;

int64_t GetBlobMemoryBytesSize(BlobMemorySizeInfo& size_info) {
    if (size_info.dims.size() == 1) {
        int64_t dims_count = DimsVectorUtils::Count(size_info.dims);
        if (dims_count < 0)
            return -1;
        int64_t bytes = dims_count * DataTypeUtils::GetBytesSize(size_info.data_type);
        return (bytes < 0 || bytes > kMaxBlobBytes) ? -1 : bytes;
    } else if (size_info.dims.size() == 2) {
        int64_t dims_count = 1;
        for (auto dim : size_info.dims) {
            if (dim < 0)
                return -1;
            dims_count *= dim;
        }
        int64_t bytes = dims_count * 4 * DataTypeUtils::GetBytesSize(size_info.data_type);
        return (bytes < 0 || bytes > kMaxBlobBytes) ? -1 : bytes;
    } else {
        return 0;
    }
}

}  // namespace cosmo::nn