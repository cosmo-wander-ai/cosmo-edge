#pragma once

#include "nn/device/sophon/sophon_affine_crop_node.h"
#include "nn/device/sophon/sophon_copy_node.h"
#include "nn/device/sophon/sophon_crop_resize_node.h"
#include "nn/device/sophon/sophon_dino_encode_node.h"
#include "nn/device/sophon/sophon_net_node.h"
#include "nn/device/sophon/sophon_normalize_node.h"
#include "nn/device/sophon/sophon_resize_node.h"
#include "nn/device/sophon/sophon_sequence_node.h"
#include "nn/device/sophon/sophon_yolo_decode_npu_node.h"

#define ALIGN(x, a) (((x) + ((a)-1)) & ~((a)-1))

#define RGBU8_IMAGE_SIZE(width, height) ((width) * (height)*3)

#define BM_IMAGE_FORMAT FORMAT_BGR_PACKED
#define USE_MEM_HEAP0 0  // for bmrt
#define USE_MEM_HEAP1 1  // for bmcv
#define HEAP_MASK_USE_HEAP0 1
#define HEAP_MASK_USE_HEAP1 2
#define HEAP_MASK_USE_HEAP0_HEAP1 3

static std::string shape_to_str(const bm_shape_t& shape) {
    std::string str = "[ ";
    for (int i = 0; i < shape.num_dims; i++) {
        str += std::to_string(shape.dims[i]) + " ";
    }
    str += "]";
    return str;
}
