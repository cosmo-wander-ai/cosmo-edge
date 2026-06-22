#include "nn/node/sam_decode_node.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "nn/node/node_type_utils.h"
#include "nn/utils/data_type_utils.h"
#include "nn/utils/dims_vector_utils.h"

namespace cosmo::nn {

SAMDecodeNode::SAMDecodeNode() : Node() {
    node_type     = NodeType::NODE_SAM_DECODE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_SAM_DECODE).append("_0");
    one_blob_only = false;
    threshold     = 0.5f;          // Default threshold
    output_size   = {1024, 1024};  // Default output size
}

SAMDecodeNode::~SAMDecodeNode() {}

void SAMDecodeNode::LoadParam(Op* op) {
    if (!op)
        return;

    SAMDecode* op_decode = dynamic_cast<SAMDecode*>(op);
    threshold            = op_decode->threshold;
    if (threshold <= 0.0f) {
        threshold = 0.5f;
    }
    output_size = op_decode->output_size;
}

size_t SAMDecodeNode::GetTopCount() {
    return 1;
}

size_t SAMDecodeNode::GetBottomCount() {
    return 2;  // masks and scores
}

Status SAMDecodeNode::InferTopShapes() {
    // Default output shape
    int out_h = output_size.size() >= 2 ? output_size[0] : 1024;
    int out_w = output_size.size() >= 2 ? output_size[1] : 1024;

    top_blob_data_types = {DataType::DATA_TYPE_UINT8};
    top_blob_shapes     = {{1, out_h, out_w}};
    return COSMO_NN_OK;
}

bool SAMDecodeNode::NeedBottomShapesInfered() {
    // If this is a first_calculate_node (connected to InputNode),
    // we don't have bottom blobs yet, so use default shapes
    if (first_calculate_node) {
        return false;
    }
    // Otherwise, we need bottom shapes to infer output shapes
    return true;
}

Status SAMDecodeNode::InferTopShapesWithBottoms(std::vector<DimsVector> dims, std::vector<DataType> types) {
    if (dims.size() < 2) {
        return Status(COSMO_NN_ERR_PARAM, "sam_decode needs 2 bottom blobs: masks and scores");
    }

    // Input 0: masks [batch, num_masks, H, W]
    // Input 1: scores [batch, num_masks]
    // Output: [batch, out_h, out_w]
    const int batch = dims[0].at(0);

    int out_h = output_size.size() >= 2 ? output_size[0] : 1024;
    int out_w = output_size.size() >= 2 ? output_size[1] : 1024;

    // If masks are 4D, use the H and W from masks
    if (dims[0].size() == 4) {
        // Use mask dimensions if output_size not specified
        if (output_size.size() < 2) {
            out_h = dims[0].at(2);
            out_w = dims[0].at(3);
        }
    }

    top_blob_data_types = {DataType::DATA_TYPE_UINT8};
    top_blob_shapes     = {{batch, out_h, out_w}};

    return COSMO_NN_OK;
}

void SAMDecodeNode::ResizeMask(float* input_mask, int in_h, int in_w, uint8_t* output_mask, int out_h,
                               int out_w) {
    // Simple nearest neighbor resize
    // Input mask contains probabilities (0-1) after sigmoid
    float scale_h = static_cast<float>(in_h) / out_h;
    float scale_w = static_cast<float>(in_w) / out_w;

    for (int i = 0; i < out_h; ++i) {
        int src_i = static_cast<int>(i * scale_h);
        src_i     = std::min(src_i, in_h - 1);

        for (int j = 0; j < out_w; ++j) {
            int src_j = static_cast<int>(j * scale_w);
            src_j     = std::min(src_j, in_w - 1);

            float prob                 = input_mask[src_i * in_w + src_j];
            output_mask[i * out_w + j] = (prob > threshold) ? 255 : 0;
        }
    }
}

Status SAMDecodeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                              std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();
    RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blobs, top_blobs, true));

    if (bottom_blobs.size() < 2) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "sam_decode needs 2 bottom blobs: masks and scores");
    }

    // Identify masks and scores by shape (not by order)
    // 4D tensor: masks [batch, num_masks, H, W]
    // 2D tensor: scores [batch, num_masks]
    std::shared_ptr<Blob> masks_blob, scores_blob;
    BlobDesc masks_desc, scores_desc;
    BlobHandle masks_handle, scores_handle;
    DimsVector masks_dims, scores_dims;

    for (size_t i = 0; i < bottom_blobs.size(); i++) {
        auto dims = bottom_blobs.at(i)->GetBlobDesc().dims;
        if (dims.size() == 4 && !masks_blob) {
            // 4D tensor: masks
            masks_blob   = bottom_blobs.at(i);
            masks_desc   = masks_blob->GetBlobDesc();
            masks_handle = masks_blob->GetHandle();
            masks_dims   = masks_desc.dims;
        } else if (dims.size() == 2 && !scores_blob) {
            // 2D tensor: scores
            scores_blob   = bottom_blobs.at(i);
            scores_desc   = scores_blob->GetBlobDesc();
            scores_handle = scores_blob->GetHandle();
            scores_dims   = scores_desc.dims;
        }
    }

    if (!masks_blob || !scores_blob) {
        return Status(COSMO_NN_ERR_INVALID_INPUT,
                      "Failed to identify masks (4D) and scores (2D) from bottom_blobs");
    }

    // Read data pointers
    float* masks_data = reinterpret_cast<float*>(masks_handle.base);

    // Handle different data types for scores
    std::vector<float> scores_buffer;  // Buffer for converted scores if needed
    float* scores_data = nullptr;

    int num_scores = scores_dims.size() > 1 ? scores_dims[1] : scores_dims[0];

    if (scores_desc.data_type == DataType::DATA_TYPE_INT32) {
        int32_t* scores_int = reinterpret_cast<int32_t*>(scores_handle.base);
        scores_buffer.resize(num_scores);
        for (int i = 0; i < num_scores; i++) {
            scores_buffer[i] = static_cast<float>(scores_int[i]);
        }
        scores_data = scores_buffer.data();
    } else if (scores_desc.data_type == DataType::DATA_TYPE_FLOAT) {
        scores_data = reinterpret_cast<float*>(scores_handle.base);
    } else {
        scores_data = reinterpret_cast<float*>(scores_handle.base);
    }

    if (!scores_data) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Failed to get scores data");
    }

    // Use the identified masks and scores directly (no swapping needed since we identified by shape)
    DimsVector actual_masks_dims  = masks_dims;
    DimsVector actual_scores_dims = scores_dims;
    float* actual_masks_data      = masks_data;
    float* actual_scores_data     = scores_data;

    const int batch = actual_masks_dims.at(0);
    if (batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size too large");

    // Parse masks dimensions: [batch, num_masks, H, W]
    if (actual_masks_dims.size() != 4) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "masks must be 4D: [batch, num_masks, H, W]");
    }

    const int num_masks = actual_masks_dims.at(1);
    const int mask_h    = actual_masks_dims.at(2);
    const int mask_w    = actual_masks_dims.at(3);
    const int mask_size = mask_h * mask_w;

    // Parse scores dimensions: [batch, num_masks]
    if (actual_scores_dims.size() != 2) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "scores must be 2D: [batch, num_masks]");
    }
    if (actual_scores_dims.at(0) != batch) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "scores batch size mismatch");
    }
    if (actual_scores_dims.at(1) != num_masks) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "scores num_masks mismatch");
    }

    // Get output dimensions from config
    int out_h = output_size.size() >= 2 ? output_size[0] : mask_h;
    int out_w = output_size.size() >= 2 ? output_size[1] : mask_w;

    auto output_mask = top_blobs.at(0);
    SetCurrentBatch(output_mask, batch);

    auto output_handle = output_mask->GetHandle();
    auto output_data   = reinterpret_cast<uint8_t*>(output_handle.base);

    for (int b = 0; b < batch; ++b) {
        float* batch_scores = actual_scores_data + b * num_masks;
        float* batch_masks  = actual_masks_data + b * num_masks * mask_size;

        int best_mask_idx = 0;
        float best_score  = batch_scores[0];
        for (int i = 1; i < num_masks; ++i) {
            if (batch_scores[i] > best_score) {
                best_score    = batch_scores[i];
                best_mask_idx = i;
            }
        }

        float* best_mask_logits = batch_masks + best_mask_idx * mask_size;

        std::vector<float> best_mask_probs(mask_size);
        for (int i = 0; i < mask_size; ++i) {
            float logit        = best_mask_logits[i];
            best_mask_probs[i] = 1.0f / (1.0f + std::exp(-logit));
        }

        // Resize and threshold the mask
        uint8_t* batch_output = output_data + b * out_h * out_w;
        ResizeMask(best_mask_probs.data(), mask_h, mask_w, batch_output, out_h, out_w);
    }

    timer.Stop();
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn
