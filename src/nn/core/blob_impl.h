#pragma once

#include "nn/core/blob.h"
#include "nn/core/macros.h"

namespace cosmo::nn {

// data store
class PUBLIC BlobImpl {
public:
    explicit BlobImpl(BlobDesc desc_);

    BlobImpl(BlobDesc desc_, bool alloc_memory_);

    BlobImpl(BlobDesc desc_, BlobHandle handle_);

    virtual ~BlobImpl();

    BlobDesc& GetBlobDesc();

    void SetBlobDesc(BlobDesc desc_);

    BlobHandle GetHandle();

    void SetHandle(BlobHandle handle_);

private:
    BlobDesc desc;
    BlobHandle handle;
    bool alloc_memory;
};

}  // namespace cosmo::nn
