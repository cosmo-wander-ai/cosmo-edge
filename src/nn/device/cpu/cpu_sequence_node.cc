#ifdef COSMO_NN_USE_CPU_BACKEND

#include "nn/device/cpu/cpu_sequence_node.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"
#include "nn/utils/op.h"
#include "nn/utils/rect.h"

namespace cosmo::nn {

CpuSequenceNode::CpuSequenceNode() : Node() {
    node_type     = NodeType::NODE_SEQUENCE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_SEQUENCE).append("_0");
    one_blob_only = true;
}

void CpuSequenceNode::LoadParam(Op* op) {
    if (!op)
        return;
    auto sequence_op = dynamic_cast<Sequence*>(op);
    if (!sequence_op)
        return;

    dst_batch  = max_batch;
    dst_depth  = sequence_op->size;
    dst_height = sequence_op->dsize.at(0);
    dst_width  = sequence_op->dsize.at(1);
    scale      = sequence_op->scale;
    is_bgr     = sequence_op->is_bgr;
}

DeviceType CpuSequenceNode::GetTopBlobDeviceType() {
    return DeviceType::DEVICE_NAIVE;
}

Status CpuSequenceNode::InferTopShapes() {
    // Output: NCHW float32 (stitched + normalized)
    top_blob_shapes     = {{max_batch, 3, dst_height, dst_width}};
    top_blob_data_types = {DataType::DATA_TYPE_FLOAT};
    return COSMO_NN_OK;
}

size_t CpuSequenceNode::GetTopCount() {
    return 1;
}

size_t CpuSequenceNode::GetBottomCount() {
    return 2;
}

Status CpuSequenceNode::PrepareRect(std::shared_ptr<Blob> host_rect, int image_w, int image_h) {
    calculated_rects.clear();

    auto desc = host_rect->GetBlobDesc();
    if (desc.data_type != DataType::DATA_TYPE_INT32)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "rect data type must be int32.");

    auto dims = desc.dims;
    if (dims.at(1) != 4)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "rect shape must be [n, 4].");

    size_t n                = dims.at(0);
    int32_t* host_rect_data = reinterpret_cast<int32_t*>(host_rect->GetHandle().base);

    for (size_t i = 0; i < n; ++i) {
        int x1 = std::max(0, host_rect_data[0]);
        int y1 = std::max(0, host_rect_data[1]);
        int x2 = std::min(x1 + host_rect_data[2] - 1, image_w - 1);
        int y2 = std::min(y1 + host_rect_data[3] - 1, image_h - 1);
        host_rect_data += 4;

        calculated_rects.push_back(x1);
        calculated_rects.push_back(y1);
        calculated_rects.push_back(x2 - x1 + 1);
        calculated_rects.push_back(y2 - y1 + 1);
    }
    return COSMO_NN_OK;
}

void CpuSequenceNode::BilinearResize(const uint8_t* src, int src_w, int src_h, int channels, uint8_t* dst,
                                     int dst_w, int dst_h) {
    const float x_ratio = static_cast<float>(src_w) / dst_w;
    const float y_ratio = static_cast<float>(src_h) / dst_h;

    for (int dy = 0; dy < dst_h; dy++) {
        float fy     = (dy + 0.5f) * y_ratio - 0.5f;
        int sy       = static_cast<int>(fy);
        float frac_y = fy - sy;
        sy           = std::max(0, std::min(sy, src_h - 1));
        int sy1      = std::min(sy + 1, src_h - 1);

        for (int dx = 0; dx < dst_w; dx++) {
            float fx     = (dx + 0.5f) * x_ratio - 0.5f;
            int sx       = static_cast<int>(fx);
            float frac_x = fx - sx;
            sx           = std::max(0, std::min(sx, src_w - 1));
            int sx1      = std::min(sx + 1, src_w - 1);

            for (int c = 0; c < channels; c++) {
                float v00 = src[(sy * src_w + sx) * channels + c];
                float v01 = src[(sy * src_w + sx1) * channels + c];
                float v10 = src[(sy1 * src_w + sx) * channels + c];
                float v11 = src[(sy1 * src_w + sx1) * channels + c];

                float val = v00 * (1 - frac_x) * (1 - frac_y) + v01 * frac_x * (1 - frac_y) +
                            v10 * (1 - frac_x) * frac_y + v11 * frac_x * frac_y;

                dst[(dy * dst_w + dx) * channels + c] =
                    static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, val + 0.5f)));
            }
        }
    }
}

Status CpuSequenceNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                                std::vector<std::shared_ptr<Blob>>& params,
                                std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();

    const int image_count = bottom_blobs.size();
    const int rect_count  = params.size();
    if (image_count != dst_depth || rect_count != dst_depth)
        return Status(COSMO_NN_ERR_PARAM, "Input size not match with dst_depth");

    auto top_blob  = top_blobs.at(0);
    float* top_ptr = static_cast<float*>(top_blob->GetHandle().base);

    int channels = 3;

    // Calculate stitch grid
    int hw              = static_cast<int>(std::sqrt(static_cast<float>(image_count)));
    int single_resize_h = dst_height / hw;
    int single_resize_w = dst_width / hw;

    // Create temporary stitched uint8 buffer (HWC)
    std::vector<uint8_t> stitch_buf(dst_height * dst_width * channels, 0);

    for (int i = 0; i < image_count; i++) {
        auto bottom    = bottom_blobs.at(i);
        auto b_dims    = bottom->GetBlobDesc().dims;
        int src_h      = b_dims.at(1);
        int src_w      = b_dims.at(2);
        auto* src_data = static_cast<const uint8_t*>(bottom->GetHandle().base);

        auto rect_blob = params.at(i);
        RETURN_ON_FAIL(PrepareRect(rect_blob, src_w, src_h));

        int rx = calculated_rects.at(0);
        int ry = calculated_rects.at(1);
        int rw = calculated_rects.at(2);
        int rh = calculated_rects.at(3);

        // Crop
        std::vector<uint8_t> crop(rw * rh * channels);
        for (int y = 0; y < rh; y++) {
            std::memcpy(crop.data() + y * rw * channels, src_data + ((ry + y) * src_w + rx) * channels,
                        rw * channels);
        }

        // Resize crop to single_resize size
        std::vector<uint8_t> resized(single_resize_w * single_resize_h * channels);
        BilinearResize(crop.data(), rw, rh, channels, resized.data(), single_resize_w, single_resize_h);

        // Place into stitch buffer
        int grid_x = i % hw;
        int grid_y = i / hw;
        int off_x  = grid_x * single_resize_w;
        int off_y  = grid_y * single_resize_h;
        for (int y = 0; y < single_resize_h; y++) {
            std::memcpy(stitch_buf.data() + ((off_y + y) * dst_width + off_x) * channels,
                        resized.data() + y * single_resize_w * channels, single_resize_w * channels);
        }
    }

    // Convert HWC uint8 to NCHW float32 with scale (1/255)
    float norm_scale = 1.0f / 255.0f;
    int spatial      = dst_height * dst_width;
    for (int c = 0; c < channels; c++) {
        for (int y = 0; y < dst_height; y++) {
            for (int x = 0; x < dst_width; x++) {
                top_ptr[c * spatial + y * dst_width + x] =
                    stitch_buf[(y * dst_width + x) * channels + c] * norm_scale;
            }
        }
    }

    timer.Stop();
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_CPU_BACKEND
