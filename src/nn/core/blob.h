#pragma once

#include <map>
#include <memory>
#include <string>

#include "nn/core/common.h"
#include "nn/core/macros.h"
#include "nn/utils/image_format_utils.h"

namespace cosmo::nn {

struct PUBLIC BlobDesc {
    DeviceType device_type = DEVICE_NAIVE;

    DataType data_type = DATA_TYPE_FLOAT;

    DataFormat data_format   = DATA_FORMAT_NCHW;
    ImageFormat image_format = IMAGE_UNKNOWN;

    DimsVector dims;

    std::string name = "";

    std::string description();
};

struct PUBLIC BlobHandle {
    void* base        = nullptr;
    unsigned long phy = 0;
};

class BlobImpl;

class PUBLIC Blob {
public:
    explicit Blob(BlobDesc desc);

    Blob(BlobDesc desc, bool alloc_memory);

    Blob(BlobDesc desc, BlobHandle handle);

    ~Blob();

    BlobDesc& GetBlobDesc();

    void SetBlobDesc(BlobDesc desc);

    BlobHandle GetHandle();

    void SetHandle(BlobHandle handle);

private:
    std::unique_ptr<BlobImpl> impl;
};

using BlobMap = std::map<std::string, std::shared_ptr<Blob>>;

}  // namespace cosmo::nn
