#ifdef COSMO_NN_USE_CPU_BACKEND

#include "nn/device/cpu/cpu_normalize_node.h"

#include <algorithm>

#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"
#include "nn/utils/op.h"

namespace cosmo::nn {

CpuNormalizeNode::CpuNormalizeNode() : Node() {
    node_type     = NodeType::NODE_NORMALIZE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_NORMALIZE).append("_0");
    one_blob_only = true;
}

void CpuNormalizeNode::LoadParam(Op* op) {
    if (!op)
        return;
    auto norm = dynamic_cast<Normalize*>(op);
    if (!norm)
        return;

    is_bgr_ = norm->is_bgr;
    mean_   = norm->mean;

    if (norm->std.empty()) {
        // Use uniform scale
        scale_.assign(mean_.size(), norm->scale);
    } else {
        // scale = 1.0 / std
        scale_.resize(norm->std.size());
        std::transform(norm->std.begin(), norm->std.end(), scale_.begin(), [](float v) { return 1.0f / v; });
    }
}

bool CpuNormalizeNode::NeedBottomShapesInfered() {
    return true;
}

Status CpuNormalizeNode::InferTopShapesWithBottoms(std::vector<DimsVector> dims,
                                                   std::vector<DataType> types) {
    auto bottom_shape = dims.at(0);

    // Input is NHWC, output is NCHW
    bottom_shape.at(1) = shared_resource->net_input_h;
    bottom_shape.at(2) = shared_resource->net_input_w;

    top_blob_shapes     = {DimsVectorUtils::NHWC2NCHW(bottom_shape)};
    top_blob_data_types = {DataType::DATA_TYPE_FLOAT};
    return COSMO_NN_OK;
}

size_t CpuNormalizeNode::GetBottomCount() {
    return 1;
}
size_t CpuNormalizeNode::GetTopCount() {
    return 1;
}

DeviceType CpuNormalizeNode::GetTopBlobDeviceType() {
    return DeviceType::DEVICE_NAIVE;
}

bool CpuNormalizeNode::NeedSwapRB(ImageFormat fmt) {
    if (fmt == ImageFormat::IMAGE_BGR || fmt == ImageFormat::IMAGE_BGRA)
        return !is_bgr_;
    if (fmt == ImageFormat::IMAGE_RGB || fmt == ImageFormat::IMAGE_RGBA)
        return is_bgr_;
    return false;
}

Status CpuNormalizeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                                 std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();

    auto bottom_blob = bottom_blobs.at(0);
    auto top_blob    = top_blobs.at(0);
    RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blob, top_blob, true));

    auto bottom_desc = bottom_blob->GetBlobDesc();
    auto bottom_dim  = bottom_desc.dims;

    int batch = bottom_dim.at(0);
    if (batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size too large");

    if (mean_.size() < 3 || scale_.size() < 3)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "mean/scale size must be >= 3");

    // Set top blob batch
    SetCurrentBatch(top_blob, batch);

    auto top_desc    = top_blob->GetBlobDesc();
    auto top_dim     = top_desc.dims;
    int dst_c        = top_dim.at(1);
    int dst_h        = top_dim.at(2);
    int dst_w        = top_dim.at(3);
    int src_h        = bottom_dim.at(1);
    int src_w        = bottom_dim.at(2);
    int src_channels = (bottom_dim.size() > 3) ? bottom_dim.at(3) : 3;

    bool swap_rb = NeedSwapRB(bottom_desc.image_format);
    auto* src    = static_cast<const uint8_t*>(bottom_blob->GetHandle().base);
    auto* dst    = static_cast<float*>(top_blob->GetHandle().base);

    int hw = dst_h * dst_w;

    for (int b = 0; b < batch; b++) {
        const uint8_t* src_batch = src + b * src_h * src_w * src_channels;
        float* dst_batch         = dst + b * dst_c * hw;

        // NHWC uint8 -> NCHW float with mean/scale normalization
        for (int h = 0; h < dst_h; h++) {
            for (int w = 0; w < dst_w; w++) {
                int src_idx = (h * src_w + w) * src_channels;

                // Channel mapping: swap R and B if needed
                int ch0 = swap_rb ? 2 : 0;
                int ch2 = swap_rb ? 0 : 2;

                float v0 = static_cast<float>(src_batch[src_idx + ch0]);
                float v1 = static_cast<float>(src_batch[src_idx + 1]);
                float v2 = static_cast<float>(src_batch[src_idx + ch2]);

                dst_batch[0 * hw + h * dst_w + w] = (v0 - mean_[0]) * scale_[0];
                dst_batch[1 * hw + h * dst_w + w] = (v1 - mean_[1]) * scale_[1];
                dst_batch[2 * hw + h * dst_w + w] = (v2 - mean_[2]) * scale_[2];
            }
        }
    }

    timer.Stop();
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_CPU_BACKEND
