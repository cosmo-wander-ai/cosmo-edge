#include "nn/device/sophon/sophon_node_creator.h"

#include "nn/device/sophon/sophon_node.h"

namespace cosmo::nn {

SophonNodeCreator::SophonNodeCreator(DeviceType device_type) : NodeCreator(device_type) {}

SophonNodeCreator::~SophonNodeCreator() {}

std::unique_ptr<Node> SophonNodeCreator::CreateNode(NodeType type) {
    switch (type) {
        case NODE_RESIZE:
            return std::make_unique<SophonResizeNode>();
        case NODE_CROP_RESIZE:
            return std::make_unique<SophonCropResizeNode>();
        case NODE_AFFINE_CROP:
            return std::make_unique<SophonAffineCropNode>();
        case NODE_SEQUENCE:
            return std::make_unique<SophonSequenceNode>();
        case NODE_NORMALIZE:
            return std::make_unique<SophonNormalizeNode>();
        // case NODE_COMBINE_IMAGE:
        //     return std::make_unique<SophonCombineImageNode>();
        case NODE_NET:
            return std::make_unique<SophonNetNode>();
        case NODE_YOLO_NPU_DECODE:
            return std::make_unique<SophonYoloDecodeNPUNode>();
        case NODE_COPY:
            return std::make_unique<SophonCopyNode>();
        case NODE_DINO_ENCODE:
            return std::make_unique<SophonDinoEncodeNode>();
        default:
            return nullptr;
    }
}

NodeCreatorRegister<SophonNodeCreator> g_sophon_node_creator_register(DEVICE_SOPHON_TPU);

}  // namespace cosmo::nn