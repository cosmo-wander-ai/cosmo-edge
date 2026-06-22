#include "nn/node/host_node.h"
#include "nn/node/host_node_creater.h"

namespace cosmo::nn {

HostNodeCreator::HostNodeCreator(DeviceType device_type) : NodeCreator(device_type) {}

HostNodeCreator::~HostNodeCreator() {}

std::unique_ptr<Node> HostNodeCreator::CreateNode(NodeType type) {
    switch (type) {
        case NODE_INPUT:
            return std::make_unique<InputNode>();
        case NODE_YOLO_DECODE:
            return std::make_unique<YoloDecodeNode>();
        case NODE_YOLOV8_DECODE:
            return std::make_unique<YoloV8DecodeNode>();
        case NODE_YOLO_E2E_DECODE:
            return std::make_unique<YoloE2EDecodeNode>();
        case NODE_SPLIT:
            return std::make_unique<SplitNode>();
        case NODE_SPLIT_ARG_MAX:
            return std::make_unique<SplitArgMaxNode>();

        case NODE_SUM:
            return std::make_unique<SumNode>();

        case NODE_CONCAT:
            return std::make_unique<ConcatNode>();

        case NODE_MEAN:
            return std::make_unique<MeanNode>();

        case NODE_DINO_DECODE:
            return std::make_unique<DinoDecodeNode>();

        case NODE_SAM_PROMPT_ENCODE:
            return std::make_unique<SAMPromptEncodeNode>();

        case NODE_SAM_DECODE:
            return std::make_unique<SAMDecodeNode>();

        case NODE_IDENTITY:
            return std::make_unique<IdentityNode>();

        default:
            return nullptr;
    }
}

NodeCreatorRegister<HostNodeCreator> g_host_node_creator_register(DEVICE_NAIVE);

}  // namespace cosmo::nn