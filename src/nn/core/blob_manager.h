#pragma once

#include "nn/core/abstract_device.h"
#include "nn/core/blob.h"
#include "nn/core/status.h"

namespace cosmo::nn {

class BlobManager {
public:
    explicit BlobManager(AbstractDevice* device);

    virtual ~BlobManager();

    Status Init(ShapesMap inputs_shape_map, DataType input_data_type, ShapesMap outputs_shape_map,
                DataType output_data_type);

    Blob* GetBlob(std::string name);

    Status GetAllInputBlobs(BlobMap& blobs);

    Status GetAllOutputBlobs(BlobMap& blobs);

    virtual Status AllocateBlobMemory() = 0;

protected:
    AbstractDevice* device;

    BlobMap input_blobs;
    BlobMap output_blobs;
};
}  // namespace cosmo::nn
