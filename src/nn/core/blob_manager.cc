#include "nn/core/blob_manager.h"

#include <memory>

namespace cosmo::nn {

BlobManager::BlobManager(AbstractDevice* device_) {
    device = device_;
}

BlobManager::~BlobManager() {
    for (auto& iter : input_blobs) {
        if (iter.second) {
            device->Free(iter.second->GetHandle());
        }
    }

    for (auto& iter : output_blobs) {
        if (iter.second) {
            device->Free(iter.second->GetHandle());
        }
    }
}

Status BlobManager::Init(ShapesMap inputs_shape_map, DataType input_data_type, ShapesMap outputs_shape_map,
                         DataType output_data_type) {
    // input
    for (auto iter : inputs_shape_map) {
        BlobDesc desc;
        desc.device_type = device->GetDeviceType();
        desc.data_type   = input_data_type;
        desc.name        = iter.first;
        desc.dims        = iter.second;

        BlobHandle handle;

        input_blobs[iter.first] = std::make_shared<Blob>(desc, handle);
    }

    // output
    for (auto iter : outputs_shape_map) {
        BlobDesc desc;
        desc.device_type = device->GetDeviceType();
        desc.data_type   = output_data_type;
        desc.name        = iter.first;
        desc.dims        = iter.second;

        BlobHandle handle;

        output_blobs[iter.first] = std::make_shared<Blob>(desc, handle);
    }

    return COSMO_NN_OK;
}

Status BlobManager::GetAllInputBlobs(BlobMap& blobs) {
    blobs = input_blobs;
    return COSMO_NN_OK;
}

Status BlobManager::GetAllOutputBlobs(BlobMap& blobs) {
    blobs = output_blobs;
    return COSMO_NN_OK;
}

Blob* BlobManager::GetBlob(std::string name) {
    auto iter = input_blobs.find(name);
    if (iter != input_blobs.end()) {
        return iter->second.get();
    }

    iter = output_blobs.find(name);
    if (iter != output_blobs.end()) {
        return iter->second.get();
    }
    return nullptr;
}

}  // namespace cosmo::nn