#pragma once

#include <string>

#include "nn/core/common.h"
#include "nn/core/macros.h"

namespace cosmo::nn {

class PUBLIC DataTypeUtils {
public:
    static int GetBytesSize(DataType);

    static std::string GetDataTypeString(DataType);
};

/**
 * Convert InputNodeInfo raw data_type integer to DataType enum.
 * InputNodeInfo encoding: 0=fp32, 1=fp16, 2=bfp16, 3=int8
 */
inline DataType DataTypeFromInputInfo(int raw) {
    switch (raw) {
        case 1:
            return DATA_TYPE_HALF;
        case 2:
            return DATA_TYPE_BFP16;
        case 3:
            return DATA_TYPE_INT8;
        default:
            return DATA_TYPE_FLOAT;
    }
}

}  // namespace cosmo::nn
