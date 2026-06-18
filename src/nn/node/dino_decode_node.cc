#include "nn/node/dino_decode_node.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "nn/node/node_type_utils.h"
#include "nn/utils/data_type_utils.h"
#include "nn/utils/dims_vector_utils.h"
#include "nn/utils/net_utils.h"

namespace cosmo::nn {

DinoDecodeNode::DinoDecodeNode() : Node() {
    node_type     = NodeType::NODE_DINO_DECODE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_DINO_DECODE).append("_0");
    one_blob_only = false;
}

DinoDecodeNode::~DinoDecodeNode() {}

void DinoDecodeNode::LoadParam(Op* op) {
    if (!op)
        return;

    DinoDecode* op_decode = dynamic_cast<DinoDecode*>(op);
    text_threshold        = op_decode->text_threshold;
    box_threshold         = op_decode->box_threshold;
}

size_t DinoDecodeNode::GetTopCount() {
    return 2;
}

size_t DinoDecodeNode::GetBottomCount() {
    return 2;
}

Status DinoDecodeNode::InferTopShapes() {
    // Default shapes, will be overridden by InferTopShapesWithBottoms if needed
    top_blob_data_types = {DataType::DATA_TYPE_FLOAT, DataType::DATA_TYPE_FLOAT};
    top_blob_shapes     = {{1, 900, 256}, {1, 900, 4}};

    return COSMO_NN_OK;
}

bool DinoDecodeNode::NeedBottomShapesInfered() {
    return true;
}

Status DinoDecodeNode::InferTopShapesWithBottoms(std::vector<DimsVector> dims, std::vector<DataType> types) {
    if (dims.size() < 2) {
        return Status(COSMO_NN_ERR_PARAM, "dino_decode needs 2 bottom blobs");
    }

    // Get shapes from bottom blobs
    // bottom[0]: logits [batch, num_boxes, num_tokens]
    // bottom[1]: boxes [batch, num_boxes, 4]
    const int batch      = dims[0].at(0);
    const int num_boxes  = dims[0].at(1);
    const int num_tokens = dims[0].at(2);

    top_blob_data_types = {DataType::DATA_TYPE_FLOAT, DataType::DATA_TYPE_FLOAT};
    top_blob_shapes     = {{batch, num_boxes, num_tokens}, {batch, num_boxes, 4}};

    return COSMO_NN_OK;
}

Status DinoDecodeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                               std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();
    RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blobs, top_blobs, true));

    auto logits = bottom_blobs.at(0);
    auto boxes  = bottom_blobs.at(1);

    auto logits_desc   = logits->GetBlobDesc();
    auto logits_handle = logits->GetHandle();
    auto logits_dims   = logits_desc.dims;
    auto logits_data   = reinterpret_cast<float*>(logits_handle.base);

    auto boxes_desc   = boxes->GetBlobDesc();
    auto boxes_handle = boxes->GetHandle();
    auto boxes_dims   = boxes_desc.dims;
    auto boxes_data   = reinterpret_cast<float*>(boxes_handle.base);

    const int batch = logits_dims.at(0);
    if (batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size too large");

    auto top_sigmoid = top_blobs.at(0);
    auto top_box     = top_blobs.at(1);
    SetCurrentBatch(top_sigmoid, batch);
    SetCurrentBatch(top_box, batch);

    auto top_sigmoid_handle = top_sigmoid->GetHandle();
    auto top_box_handle     = top_box->GetHandle();

    auto top_sigmoid_data = reinterpret_cast<float*>(top_sigmoid_handle.base);
    auto top_box_data     = reinterpret_cast<float*>(top_box_handle.base);

    auto logits_size = DimsVectorUtils::Count(logits_dims);

    // Apply sigmoid activation to logits, matching the demo behavior
    std::transform(logits_data, logits_data + logits_size, top_sigmoid_data, [](float x) {
        // Ensure numerical stability for sigmoid computation
        const float clamped_x = std::max(-50.0f, std::min(50.0f, x));
        return 1.0f / (1.0f + std::exp(-clamped_x));
    });

    auto boxes_size = DimsVectorUtils::Count(boxes_dims);
    std::copy(boxes_data, boxes_data + boxes_size, top_box_data);

    shared_resource->text_threshold = text_threshold;
    shared_resource->box_threshold  = box_threshold;

    timer.Stop();
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn