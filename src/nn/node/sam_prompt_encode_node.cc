#include "nn/node/sam_prompt_encode_node.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "nn/node/node_type_utils.h"
#include "nn/utils/data_type_utils.h"
#include "nn/utils/dims_vector_utils.h"

namespace cosmo::nn {

SAMPromptEncodeNode::SAMPromptEncodeNode() : Node() {
    node_type     = NodeType::NODE_SAM_PROMPT_ENCODE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_SAM_PROMPT_ENCODE).append("_0");
    one_blob_only = false;
}

SAMPromptEncodeNode::~SAMPromptEncodeNode() {}

void SAMPromptEncodeNode::LoadParam(Op* op) {
    if (!op)
        return;

    SAMPromptEncode* op_encode = dynamic_cast<SAMPromptEncode*>(op);
    prompt_type                = op_encode->prompt_type;
    normalize                  = op_encode->normalize;
    encoder_size               = op_encode->encoder_size;
    max_points                 = op_encode->max_points;
}

size_t SAMPromptEncodeNode::GetTopCount() {
    return 1;
}

size_t SAMPromptEncodeNode::GetBottomCount() {
    return 1;
}

Status SAMPromptEncodeNode::InferTopShapes() {
    // Default shape: [batch, max_points, 2]
    top_blob_data_types = {DataType::DATA_TYPE_FLOAT};
    top_blob_shapes     = {{1, max_points, 2}};
    return COSMO_NN_OK;
}

bool SAMPromptEncodeNode::NeedBottomShapesInfered() {
    // If this is a first_calculate_node (connected to InputNode),
    // we don't have bottom blobs yet, so use default shapes
    if (first_calculate_node) {
        return false;
    }
    // Otherwise, we need bottom shapes to infer output shapes
    return true;
}

Status SAMPromptEncodeNode::InferTopShapesWithBottoms(std::vector<DimsVector> dims,
                                                      std::vector<DataType> types) {
    if (dims.empty()) {
        return Status(COSMO_NN_ERR_PARAM, "sam_prompt_encode needs at least 1 bottom blob");
    }

    // Input: [batch, num_points, 2]
    // Output: [batch, max_points, 2] (padded to max_points)
    const int batch = dims[0].at(0);

    top_blob_data_types = {DataType::DATA_TYPE_FLOAT};
    top_blob_shapes     = {{batch, max_points, 2}};

    return COSMO_NN_OK;
}

void SAMPromptEncodeNode::NormalizeCoords(float* coords, int num_points, int orig_width, int orig_height) {
    // Normalize coordinates from original image space to encoder input space
    // coords is [num_points, 2] where each point is (x, y)
    for (int i = 0; i < num_points; ++i) {
        float x = coords[i * 2];
        float y = coords[i * 2 + 1];

        // Normalize to encoder size
        coords[i * 2]     = x * encoder_size / orig_width;
        coords[i * 2 + 1] = y * encoder_size / orig_height;
    }
}

void SAMPromptEncodeNode::PadPoints(float* input_coords, int input_num, float* output_coords,
                                    int output_num) {
    // Copy valid points
    std::memcpy(output_coords, input_coords, input_num * 2 * sizeof(float));

    // Pad remaining points with (0, 0)
    for (int i = input_num; i < output_num; ++i) {
        output_coords[i * 2]     = 0.0f;
        output_coords[i * 2 + 1] = 0.0f;
    }
}

Status SAMPromptEncodeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                                    std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();
    RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blobs, top_blobs, true));

    auto input_coords = bottom_blobs.at(0);
    auto input_desc   = input_coords->GetBlobDesc();
    auto input_handle = input_coords->GetHandle();
    auto input_dims   = input_desc.dims;
    auto input_data   = reinterpret_cast<float*>(input_handle.base);

    const int batch = input_dims.at(0);
    if (batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size too large");

    const int num_points = input_dims.at(1);

    auto top_coords = top_blobs.at(0);
    SetCurrentBatch(top_coords, batch);

    auto top_handle = top_coords->GetHandle();
    auto top_data   = reinterpret_cast<float*>(top_handle.base);

    // Use encoder_size as original size when normalizing.
    // Caller should ensure coordinates are in image space; normalization
    // scales to encoder input space. If no explicit orig size available,
    // use encoder_size so scale factor is 1.0 (no scaling).
    int orig_width  = encoder_size;
    int orig_height = encoder_size;

    // Process each batch
    for (int b = 0; b < batch; ++b) {
        float* batch_input  = input_data + b * num_points * 2;
        float* batch_output = top_data + b * max_points * 2;

        // Create temporary buffer for processing
        float temp_coords[num_points * 2];
        std::memcpy(temp_coords, batch_input, num_points * 2 * sizeof(float));

        // Normalize coordinates if required
        if (normalize) {
            NormalizeCoords(temp_coords, num_points, orig_width, orig_height);
        }

        // Pad points to max_points
        if (num_points < max_points) {
            PadPoints(temp_coords, num_points, batch_output, max_points);
        } else {
            std::memcpy(batch_output, temp_coords, max_points * 2 * sizeof(float));
        }
    }

    timer.Stop();
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn
