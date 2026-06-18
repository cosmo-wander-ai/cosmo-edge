#include "nn/utils/blob_memory_size_utils.h"

#include "nn/utils/dims_vector_utils.h"

namespace cosmo::nn {

BlobMemorySizeInfo Calculate1DMemorySize(BlobDesc& desc) {
    BlobMemorySizeInfo info;
    info.data_type = desc.data_type;

    int count = 0;
    if (desc.data_format == DATA_FORMAT_NC4HW4) {
        count = desc.dims[0] * cosmo::nn::RoundUp(desc.dims[1], 4) * desc.dims[2] * desc.dims[3];
    } else {
        count = DimsVectorUtils::Count(desc.dims);
        if (ImageFormatIsYUV(desc.image_format)) {
            count = count / 2;
        }
    }
    info.dims.push_back(count);

    return info;
}

}  // namespace cosmo::nn