#include "nn/utils/model_header_info.h"

#include <stdexcept>

#include "nn/core/macros.h"

namespace cosmo::nn {

static constexpr size_t kMaxModelNum          = 31;
static constexpr long int kMaxSingleModelSize = 512L * 1024 * 1024;

std::vector<long int> GetModelSize(ModelHeaderInfo& info) {
    const char* data = info.reserved;
    int model_num    = static_cast<int>(static_cast<unsigned char>(data[0]));
    if (model_num <= 0 || model_num > static_cast<int>(kMaxModelNum)) {
        throw std::invalid_argument("GetModelSize: invalid model_num (must be 1..31)");
    }
    data++;
    const size_t size_bytes = static_cast<size_t>(model_num) * sizeof(long int);
    if (size_bytes > sizeof(info.reserved) - 1u) {
        throw std::invalid_argument("GetModelSize: model_num too large for reserved buffer");
    }
    std::vector<long int> result;
    result.resize(static_cast<size_t>(model_num));
    const long int* sizes_ptr = reinterpret_cast<const long int*>(data);
    for (int i = 0; i < model_num; i++) {
        long int sz = sizes_ptr[i];
        if (sz <= 0 || sz > kMaxSingleModelSize) {
            throw std::invalid_argument("GetModelSize: invalid model size (must be positive and <= 512MB)");
        }
        result[i] = sz;
    }
    return result;
}

}  // namespace cosmo::nn
