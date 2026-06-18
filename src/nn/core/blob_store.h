#pragma once

#include "nn/core/abstract_context.h"
#include "nn/core/abstract_device.h"
#include "nn/core/blob.h"
#include "nn/core/status.h"

namespace cosmo::nn {

/**
 * BlobStore is a blob manager. It is used to manage all blobs in the whole graph.
 * It is also responsible for memory allocation and free.
 */
class BlobStore {
public:
    explicit BlobStore(DeviceType, int);

    virtual ~BlobStore();

    void AddBlob(std::shared_ptr<Blob> blob);
    void AddBlobs(std::vector<std::shared_ptr<Blob>>& blobs);

    void SetRuntimeBatchSize(size_t batch);

    Status RemoveBlobByName(std::string name);

    void GetBlob(std::string name, std::shared_ptr<Blob>& blob);
    void GetBlobs(std::vector<std::string> names, std::vector<std::shared_ptr<Blob>>& blobs);

    Status AllocaAllBlobs();

    Status AllocaBlob(std::shared_ptr<Blob>& blob);

    Status FreeBlob(std::shared_ptr<Blob>& blob);

    Status FreeBlob(std::string name);

private:
    std::vector<std::shared_ptr<Blob>> blobs;

    DeviceType calculate_device_type;

    AbstractDevice* host_device;
    AbstractDevice* calculate_device;

    int device_id = 0;
};

}  // namespace cosmo::nn
