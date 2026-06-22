// DeviceContextSophon — Device Context Sophon implementation.

#include "bmlib_runtime.h"
#include "mem/DeviceContext.h"

namespace cosmo::mem {

DeviceContext::DeviceContext() {
    handles_.resize(2);

    bm_handle_t memory_handle = {0};
    bm_dev_request(&memory_handle, dev_id_);

    bm_handle_t media_handle = {0};
    bm_dev_request(&media_handle, dev_id_);

    handles_.at(0) = reinterpret_cast<void*>(memory_handle);
    handles_.at(1) = reinterpret_cast<void*>(media_handle);
}

DeviceContext::~DeviceContext() {
    for (auto& h : handles_) {
        bm_dev_free(reinterpret_cast<bm_handle_t>(h));
    }

    handles_.clear();
}

void* DeviceContext::GetMemoryHandle() {
    return handles_.empty() ? nullptr : handles_.at(0);
}

void* DeviceContext::GetMediaHandle() {
    return handles_.empty() ? nullptr : handles_.at(1);
}

}  // namespace cosmo::mem
