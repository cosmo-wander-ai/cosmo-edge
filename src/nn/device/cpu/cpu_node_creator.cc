#ifdef COSMO_NN_USE_CPU_BACKEND

#include "nn/device/cpu/cpu_node_creator.h"

#include "nn/device/cpu/cpu_affine_crop_node.h"
#include "nn/device/cpu/cpu_copy_node.h"
#include "nn/device/cpu/cpu_crop_resize_node.h"
#include "nn/device/cpu/cpu_dino_encode_node.h"
#include "nn/device/cpu/cpu_net_node.h"
#include "nn/device/cpu/cpu_normalize_node.h"
#include "nn/device/cpu/cpu_resize_node.h"
#include "nn/device/cpu/cpu_sequence_node.h"
#include "nn/device/cpu/cpu_yolo_decode_node.h"

namespace cosmo::nn {

CpuNodeCreator::CpuNodeCreator(DeviceType device_type) : NodeCreator(device_type) {}

std::unique_ptr<Node> CpuNodeCreator::CreateNode(NodeType type) {
    switch (type) {
        case NODE_RESIZE:
            return std::make_unique<CpuResizeNode>();
        case NODE_CROP_RESIZE:
            return std::make_unique<CpuCropResizeNode>();
        case NODE_AFFINE_CROP:
            return std::make_unique<CpuAffineCropNode>();
        case NODE_SEQUENCE:
            return std::make_unique<CpuSequenceNode>();
        case NODE_NORMALIZE:
            return std::make_unique<CpuNormalizeNode>();
        case NODE_NET:
            return std::make_unique<CpuNetNode>();
        case NODE_COPY:
            return std::make_unique<CpuCopyNode>();
        case NODE_DINO_ENCODE:
            return std::make_unique<CpuDinoEncodeNode>();
        case NODE_YOLO_NPU_DECODE:
            return std::make_unique<CpuYoloDecodeNPUNode>();
        default:
            return nullptr;
    }
}

// Auto-register CPU node creator
NodeCreatorRegister<CpuNodeCreator> g_cpu_node_creator_register(DEVICE_CPU);

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_CPU_BACKEND
