#include "nn/core/abstract_context.h"

namespace cosmo::nn {

Status AbstractContext::OnForwardBegin() {
    return COSMO_NN_OK;
}

Status AbstractContext::OnForwardEnd() {
    return COSMO_NN_OK;
}

Status AbstractContext::SetThreadNum(int num) {
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn