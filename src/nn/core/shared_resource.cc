#include "nn/core/shared_resource.h"

namespace cosmo::nn {

SharedResource::SharedResource(int id) {
    current_device_id = id;

#ifdef COSMO_NN_USE_SOPHON_BACKEND
    int ret = bm_dev_request(&m_handle, 0);
    assert(BM_SUCCESS == ret);
#endif
}

SharedResource::~SharedResource() {
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    bm_dev_free(m_handle);
#endif
}

}  // namespace cosmo::nn