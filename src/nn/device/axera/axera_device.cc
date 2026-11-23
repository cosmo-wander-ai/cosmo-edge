#ifdef COSMO_NN_USE_AXERA_BACKEND
#include "nn/device/naive/naive_device.h"

namespace cosmo::nn {
// The graph's interchange blobs are host-owned. AX DMA buffers are private to
// the network node and are never exposed as host or DMA-BUF image handles.
TypeDeviceRegister<NaiveDevice> g_axera_device_register(DEVICE_AXERA);
}  // namespace cosmo::nn
#endif
