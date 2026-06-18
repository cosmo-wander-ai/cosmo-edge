#include "nn/device/naive/naive_context.h"

namespace cosmo::nn {

Status NaiveContext::GetCommandQueue(void** queue) {
    return COSMO_NN_OK;
}

Status NaiveContext::Synchronize() {
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn