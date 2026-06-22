#include "nn/core/blob.h"

#include <sstream>

#include "nn/core/blob_impl.h"

namespace cosmo::nn {

std::string BlobDesc::description() {
    std::ostringstream os;
    // name
    os << "name: " << name;

    // data type
    os << " data type: " << data_type;

    // shape
    os << " shape: [ ";
    for (auto iter : dims) {
        os << iter << " ";
    }
    os << "]";

    return os.str();
}

Blob::Blob(BlobDesc desc) {
    impl = std::make_unique<BlobImpl>(desc);
}

Blob::Blob(BlobDesc desc, bool alloc_memory) {
    impl = std::make_unique<BlobImpl>(desc, alloc_memory);
}

Blob::Blob(BlobDesc desc, BlobHandle handle) {
    impl = std::make_unique<BlobImpl>(desc, handle);
}

Blob::~Blob() = default;

BlobDesc& Blob::GetBlobDesc() {
    return impl->GetBlobDesc();
}

void Blob::SetBlobDesc(BlobDesc desc) {
    impl->SetBlobDesc(desc);
}

BlobHandle Blob::GetHandle() {
    return impl->GetHandle();
}

void Blob::SetHandle(BlobHandle handle) {
    impl->SetHandle(handle);
}

}  // namespace cosmo::nn