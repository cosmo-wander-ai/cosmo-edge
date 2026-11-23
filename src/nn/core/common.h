#pragma once

#include <map>
#include <string>
#include <vector>

#include "nn/core/macros.h"

namespace cosmo::nn {

typedef enum {
    DATA_TYPE_FLOAT = 0,
    DATA_TYPE_INT32,
    DATA_TYPE_HALF,
    DATA_TYPE_BFP16,
    DATA_TYPE_UINT8,
    DATA_TYPE_INT8,
} DataType;

typedef enum {
    DATA_FORMAT_NCHW = 0,
    DATA_FORMAT_NHWC,
    DATA_FORMAT_NC4HW4,
} DataFormat;

typedef enum {
    DEVICE_NAIVE      = 0x0000,
    DEVICE_SOPHON_TPU = 0x0007,
    DEVICE_CPU        = 0x0010,
    DEVICE_RKNN       = 0x0011,
    DEVICE_AXERA      = 0x0012,
} DeviceType;

// Centralizes the graph's memory-boundary decision. New backends that consume
// and produce host buffers extend this capability without duplicating CPU
// checks throughout graph wiring and inference adapters.
inline bool UsesHostMemory(DeviceType device_type) {
    return device_type == DEVICE_NAIVE || device_type == DEVICE_CPU || device_type == DEVICE_RKNN ||
           device_type == DEVICE_AXERA;
}

struct PUBLIC BackendConfig {
    DeviceType device_type = DEVICE_SOPHON_TPU;
    int device_id          = 0;
    DataFormat data_format = DATA_FORMAT_NCHW;
};

using DimsVector = std::vector<int>;
using ShapesMap  = std::map<std::string, DimsVector>;

}  // namespace cosmo::nn
