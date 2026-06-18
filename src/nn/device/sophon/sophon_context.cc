#include "nn/device/sophon/sophon_context.h"

namespace cosmo::nn {

SophonContext::~SophonContext() {}

Status SophonContext::Setup(int device_id_) {
    this->device_id = device_id_;

    return COSMO_NN_OK;
}

Status SophonContext::GetCommandQueue(void** command_queue) {
    return COSMO_NN_OK;
}

Status SophonContext::Synchronize() {
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn