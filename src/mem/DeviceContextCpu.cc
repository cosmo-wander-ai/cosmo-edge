// DeviceContextCpu — Device Context Cpu implementation.

#include "mem/DeviceContext.h"

namespace cosmo::mem {

DeviceContext::DeviceContext() {}

DeviceContext::~DeviceContext() {}

void* DeviceContext::GetMemoryHandle() {
    return handles_.empty() ? nullptr : handles_.at(0);
}

void* DeviceContext::GetMediaHandle() {
    return handles_.empty() ? nullptr : handles_.at(1);
}

}  // namespace cosmo::mem
