#include "nn/core/blob_store.h"

#include <iostream>

#include "nn/device/naive/naive_device.h"
namespace cosmo::nn {

BlobStore::BlobStore(DeviceType device_type, int id) : calculate_device_type(device_type) {
    host_device      = GetDevice(DEVICE_NAIVE);
    calculate_device = GetDevice(device_type);
    device_id        = id;
}

BlobStore::~BlobStore() {
    auto it = blobs.begin();
    while (it != blobs.end()) {
        FreeBlob(*it);
        ++it;
    }

    blobs.clear();
}

void BlobStore::AddBlob(std::shared_ptr<Blob> blob) {
    if (blob)
        blobs.push_back(blob);
}

void BlobStore::AddBlobs(std::vector<std::shared_ptr<Blob>>& blobs) {
    for (auto& blob : blobs)
        AddBlob(blob);
}

void BlobStore::SetRuntimeBatchSize(size_t batch) {
    for (auto& blob : blobs) {
        auto desc       = blob->GetBlobDesc();
        desc.dims.at(0) = batch;
        blob->SetBlobDesc(desc);
    }
}

Status BlobStore::RemoveBlobByName(std::string name) {
    auto it = blobs.begin();
    while (it != blobs.end()) {
        if ((*it)->GetBlobDesc().name == name) {
            FreeBlob(*it);

            it->reset();
            it = blobs.erase(it);
        } else {
            ++it;
        }
    }

    return COSMO_NN_OK;
}

void BlobStore::GetBlob(std::string name, std::shared_ptr<Blob>& b) {
    for (auto& blob : blobs) {
        if (blob->GetBlobDesc().name == name)
            b = blob;
    }
}

void BlobStore::GetBlobs(std::vector<std::string> names, std::vector<std::shared_ptr<Blob>>& blobs) {
    size_t n = names.size();
    for (size_t i = 0; i < n; i++) {
        std::shared_ptr<Blob> b = nullptr;
        GetBlob(names.at(i), b);

        if (b)
            blobs.push_back(b);
    }
}

Status BlobStore::FreeBlob(std::string name) {
    std::shared_ptr<Blob> blob = nullptr;
    GetBlob(name, blob);
    if (!blob)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "blob not found");

    return FreeBlob(blob);
}

Status BlobStore::FreeBlob(std::shared_ptr<Blob>& blob) {
    if (!blob)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "blob is nullptr");

    auto handle = blob->GetHandle();
    if (!handle.base)
        return COSMO_NN_OK;  // Skip if never allocated, avoid Free on invalid pointer

    auto desc        = blob->GetBlobDesc();
    auto device_type = desc.device_type;
    if (device_type == DEVICE_NAIVE) {
        return host_device->Free(handle);
    } else if (device_type == calculate_device_type) {
        return calculate_device->Free(handle);
    } else {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "invalid device type");
    }
}

Status BlobStore::AllocaBlob(std::shared_ptr<Blob>& blob) {
    if (!blob)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "blob is nullptr");

    auto desc        = blob->GetBlobDesc();
    auto handle      = blob->GetHandle();
    auto device_type = desc.device_type;
    if (device_type == DEVICE_NAIVE) {
        BlobMemorySizeInfo size_info = host_device->Calculate(desc);
        RETURN_ON_FAIL(host_device->Allocate(&handle, size_info));
    } else if (device_type == calculate_device_type) {
        BlobMemorySizeInfo size_info = calculate_device->Calculate(desc);
        RETURN_ON_FAIL(calculate_device->Allocate(&handle, size_info));
    } else {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "invalid device type");
    }

    blob->SetHandle(handle);
    return COSMO_NN_OK;
}

Status BlobStore::AllocaAllBlobs() {
    for (auto& blob : blobs) {
        Status status = AllocaBlob(blob);
        if (!bool(status)) {
            return status;
        }
    }
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn