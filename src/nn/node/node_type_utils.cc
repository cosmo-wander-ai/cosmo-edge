#include "nn/node/node_type_utils.h"

#include <algorithm>
#include <iostream>

#include "nn/node/host_node.h"
#include "nn/node/net_node.h"
#include "nn/node/node_creator.h"

namespace cosmo::nn {

bool NodeTypeUtils::IsInputNode(Node* node) {
    return node->GetNodeType() == NODE_INPUT;
}

NodeType NodeTypeUtils::NodeTypeFromStr(std::string name) {
    if (name == "input")
        return NODE_INPUT;
    if (name == "resize")
        return NODE_RESIZE;
    if (name == "crop_resize")
        return NODE_CROP_RESIZE;
    if (name == "affine_crop")
        return NODE_AFFINE_CROP;
    if (name == "sequence")
        return NODE_SEQUENCE;
    if (name == "normalize")
        return NODE_NORMALIZE;
    if (name == "combine_image")
        return NODE_COMBINE_IMAGE;
    if (name == "dino_encode")
        return NODE_DINO_ENCODE;
    if (name == "net")
        return NODE_NET;
    if (name == "copy")
        return NODE_COPY;
    if (name == "yolo_postprocess" || name == "yolo_decode")
        return NODE_YOLO_DECODE;
    if (name == "yolo_npu_postprocess")
        return NODE_YOLO_NPU_DECODE;
    if (name == "yolov8_postprocess")
        return NODE_YOLOV8_DECODE;
    if (name == "yolo_e2e_postprocess")
        return NODE_YOLO_E2E_DECODE;
    if (name == "split")
        return NODE_SPLIT;
    if (name == "split_arg_max")
        return NODE_SPLIT_ARG_MAX;
    if (name == "sum")
        return NODE_SUM;
    if (name == "concat")
        return NODE_CONCAT;
    if (name == "mean")
        return NODE_MEAN;
    if (name == "dino_decode")
        return NODE_DINO_DECODE;
    if (name == "sam_encode")
        return NODE_SAM_ENCODE;
    if (name == "sam_prompt_encode")
        return NODE_SAM_PROMPT_ENCODE;
    if (name == "sam_decode")
        return NODE_SAM_DECODE;
    if (name == "identity")
        return NODE_IDENTITY;
    return NODE_UNKNOWN;
}

std::string NodeTypeUtils::NodeTypeToStr(NodeType type) {
    switch (type) {
        case NODE_INPUT:
            return "input";
        case NODE_RESIZE:
            return "resize";
        case NODE_CROP_RESIZE:
            return "crop_resize";
        case NODE_AFFINE_CROP:
            return "affine_crop";
        case NODE_SEQUENCE:
            return "sequence";
        case NODE_NORMALIZE:
            return "normalize";
        case NODE_COMBINE_IMAGE:
            return "combine_image";
        case NODE_DINO_ENCODE:
            return "dino_encode";
        case NODE_NET:
            return "net";
        case NODE_COPY:
            return "copy";
        case NODE_YOLO_DECODE:
            return "yolo_decode";
        case NODE_YOLO_NPU_DECODE:
            return "yolo_npu_decode";
        case NODE_YOLOV8_DECODE:
            return "yolov8_decode";
        case NODE_YOLO_E2E_DECODE:
            return "yolo_e2e_decode";
        case NODE_SPLIT:
            return "split";
        case NODE_SPLIT_ARG_MAX:
            return "split_arg_max";
        case NODE_SUM:
            return "sum";
        case NODE_CONCAT:
            return "concat";
        case NODE_MEAN:
            return "mean";
        case NODE_DINO_DECODE:
            return "dino_decode";
        case NODE_SAM_ENCODE:
            return "sam_encode";
        case NODE_SAM_PROMPT_ENCODE:
            return "sam_prompt_encode";
        case NODE_SAM_DECODE:
            return "sam_decode";
        case NODE_IDENTITY:
            return "identity";
        default:
            return "unknown";
    }
    return "unknown";
}

int NodeTypeUtils::TypedNodeCount(const std::vector<std::unique_ptr<Node>>& nodes, NodeType type) {
    return count_if(nodes.begin(), nodes.end(),
                    [type](const std::unique_ptr<Node>& node) { return node->GetNodeType() == type; });
}

std::unique_ptr<Node> NodeTypeUtils::CreateNode(NodeType type, int count, int max_batch, DeviceType device) {
    auto node = CreateByType(type, device);
    if (!node)
        return nullptr;

    node->SetMaxBatch(max_batch);
    node->UpdateNodeName(count);

    return node;
}

std::unique_ptr<Node> NodeTypeUtils::CreateByType(NodeType type, DeviceType device) {
    std::unique_ptr<Node> node = nullptr;

    // 1. Try the specified device's NodeCreator
    auto device_creator = GetNodeCreator(device);
    if (device_creator)
        node = device_creator->CreateNode(type);

    // 2. Fallback to HOST (NAIVE) node creator
    if (!node) {
        auto host_node_creator = GetNodeCreator(DEVICE_NAIVE);
        if (host_node_creator)
            node = host_node_creator->CreateNode(type);
    }

    return node;
}

}  // namespace cosmo::nn
