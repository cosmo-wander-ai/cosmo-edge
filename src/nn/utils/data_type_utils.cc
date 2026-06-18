#include "nn/utils/data_type_utils.h"

namespace cosmo::nn {

int DataTypeUtils::GetBytesSize(DataType type) {
    if (type == DATA_TYPE_FLOAT || type == DATA_TYPE_INT32) {
        return 4;
    } else if (type == DATA_TYPE_HALF || type == DATA_TYPE_BFP16) {
        return 2;
    } else if (type == DATA_TYPE_INT8 || type == DATA_TYPE_UINT8) {
        return 1;
    } else {
        LOGE("GetBytes Undefined.\n");
        return 0;
    }
}

std::string DataTypeUtils::GetDataTypeString(DataType type) {
    if (type == DATA_TYPE_FLOAT) {
        return "float";
    } else if (type == DATA_TYPE_INT32) {
        return "int32";
    } else if (type == DATA_TYPE_HALF) {
        return "half";
    } else if (type == DATA_TYPE_BFP16) {
        return "bfp16";
    } else if (type == DATA_TYPE_INT8) {
        return "int8";
    } else if (type == DATA_TYPE_UINT8) {
        return "uint8";
    } else {
        return "";
    }
}

}  // namespace cosmo::nn