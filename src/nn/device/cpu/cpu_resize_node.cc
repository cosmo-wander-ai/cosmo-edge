#ifdef COSMO_NN_USE_CPU_BACKEND

#include "nn/device/cpu/cpu_resize_node.h"

#include <algorithm>
#include <cstring>

#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"
#include "nn/utils/op.h"

namespace cosmo::nn {

CpuResizeNode::CpuResizeNode() : Node() {
    node_type     = NodeType::NODE_RESIZE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_RESIZE).append("_0");
    one_blob_only = true;
}

void CpuResizeNode::LoadParam(Op* op) {
    if (!op)
        return;
    auto resize = dynamic_cast<Resize*>(op);
    if (!resize)
        return;
    if (resize->dsize.size() >= 2) {
        out_h_ = resize->dsize[0];
        out_w_ = resize->dsize[1];
    }
    gravity_ = resize->gravity;
    if (!resize->color.empty())
        pad_ = static_cast<uint8_t>(resize->color[0]);
}

DeviceType CpuResizeNode::GetTopBlobDeviceType() {
    return DeviceType::DEVICE_NAIVE;
}

Status CpuResizeNode::InferTopShapes() {
    shared_resource->net_input_w = out_w_;
    shared_resource->net_input_h = out_h_;

    top_blob_shapes     = {{1, out_h_, out_w_, 3}};  // NHWC
    top_blob_data_types = {DataType::DATA_TYPE_UINT8};
    return COSMO_NN_OK;
}

size_t CpuResizeNode::GetBottomCount() {
    return 1;
}
size_t CpuResizeNode::GetTopCount() {
    return 1;
}

// ─── Bilinear interpolation (hand-written, no OpenCV) ────────────────

void CpuResizeNode::BilinearResize(const uint8_t* src, int src_w, int src_h, int channels, uint8_t* dst,
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

Status CpuResizeNode::ResizeSingle(std::shared_ptr<Blob>& bottom, std::shared_ptr<Blob>& top) {
    auto bottom_desc = bottom->GetBlobDesc();
    auto bottom_dim  = bottom_desc.dims;

    int src_h    = bottom_dim.at(1);
    int src_w    = bottom_dim.at(2);
    int channels = (bottom_dim.size() > 3) ? bottom_dim.at(3) : 3;

    auto* src_data = static_cast<const uint8_t*>(bottom->GetHandle().base);
    auto* dst_data = static_cast<uint8_t*>(top->GetHandle().base);

    if (gravity_ == 0) {
        // Stretch to target size
        BilinearResize(src_data, src_w, src_h, channels, dst_data, out_w_, out_h_);
    } else {
        // Letterbox: keep aspect ratio
        float scale = std::min(static_cast<float>(out_w_) / src_w, static_cast<float>(out_h_) / src_h);
        int new_w   = static_cast<int>(src_w * scale);
        int new_h   = static_cast<int>(src_h * scale);

        // Fill with pad color
        std::memset(dst_data, pad_, out_w_ * out_h_ * channels);

        // Resize to intermediate buffer
        std::vector<uint8_t> tmp(new_w * new_h * channels);
        BilinearResize(src_data, src_w, src_h, channels, tmp.data(), new_w, new_h);

        // Calculate offset
        int pad_x = 0, pad_y = 0;
        if (gravity_ == 1) {
            // Center
            pad_x = (out_w_ - new_w) / 2;
            pad_y = (out_h_ - new_h) / 2;
        }
        // gravity_ == 2: top-left align, pad_x = 0, pad_y = 0

        // Copy resized data into padded output
        for (int y = 0; y < new_h; y++) {
            std::memcpy(dst_data + ((pad_y + y) * out_w_ + pad_x) * channels,
                        tmp.data() + y * new_w * channels, new_w * channels);
        }
    }

    return COSMO_NN_OK;
}

Status CpuResizeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                              std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();

    auto top_blob = top_blobs.at(0);

    // Propagate image_format so downstream nodes (e.g., CpuNormalizeNode)
    // know the channel order and can apply correct BGR↔RGB swap.
    {
        auto td         = top_blob->GetBlobDesc();
        td.image_format = bottom_blobs.at(0)->GetBlobDesc().image_format;
        top_blob->SetBlobDesc(td);
    }

    if (first_calculate_node && bottom_blobs.size() > 1) {
        // Multi-image batch: process one by one into top blob
        auto top_desc = top_blob->GetBlobDesc();
        auto top_dim  = top_desc.dims;
        int batch     = static_cast<int>(bottom_blobs.size());
        if (batch > max_batch)
            return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size too large");

        top_dim.at(0) = batch;
        top_desc.dims = top_dim;
        top_blob->SetBlobDesc(top_desc);

        int single_size = out_h_ * out_w_ * 3;
        for (int i = 0; i < batch; i++) {
            // Create a temporary blob view for the i-th slice of the output
            BlobDesc slice_desc   = top_desc;
            slice_desc.dims.at(0) = 1;
            auto slice_blob       = std::make_shared<Blob>(slice_desc);
            BlobHandle handle;
            handle.base = static_cast<uint8_t*>(top_blob->GetHandle().base) + i * single_size;
            slice_blob->SetHandle(handle);

            RETURN_ON_FAIL(ResizeSingle(bottom_blobs[i], slice_blob));
        }
    } else {
        auto bottom_blob = bottom_blobs.at(0);
        RETURN_ON_FAIL(ResizeSingle(bottom_blob, top_blob));
    }

    timer.Stop();
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_CPU_BACKEND
