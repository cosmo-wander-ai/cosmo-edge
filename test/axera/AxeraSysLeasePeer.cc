#include "nn/device/axera/axera_sys.h"

// Separate translation unit exercises the shared state used by NN and media.
void* AcquirePeerLease() {
    auto* lease = new cosmo::nn::AxeraSysLease;
    if (lease->Acquire() != 0) {
        delete lease;
        return nullptr;
    }
    return lease;
}

void ReleasePeerLease(void* lease) {
    delete static_cast<cosmo::nn::AxeraSysLease*>(lease);
}
