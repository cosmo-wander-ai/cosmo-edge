#ifdef COSMO_NN_USE_CPU_BACKEND

#include "nn/device/cpu/cpu_crop_resize_node.h"

#include <algorithm>
#include <cstring>

#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"
#include "nn/utils/op.h"

namespace cosmo::nn {

CpuCropResizeNode::CpuCropResizeNode() : Node() {
    node_type     = NodeType::NODE_CROP_RESIZE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_CROP_RESIZE).append("_0");
    one_blob_only = false;
}

void CpuCropResizeNode::LoadParam(Op* op) {
    if (!op)
        return;
    auto crop_resize = dynamic_cast<CropResize*>(op);
    if (!crop_resize)
        return;

    type          = crop_resize->type;
    h_top_crop    = crop_resize->h_top_crop;
    h_bottom_crop = crop_resize->h_bottom_crop;
    w_left_crop   = crop_resize->w_left_crop;
    w_right_crop  = crop_resize->w_right_crop;
    square        = crop_resize->square;
    square_mode   = crop_resize->square_mode;
    dst_height    = crop_resize->dsize.at(0);
    dst_width     = crop_resize->dsize.at(1);
    gravity       = crop_resize->gravity;
    color         = crop_resize->color;
}

DeviceType CpuCropResizeNode::GetTopBlobDeviceType() {
    return DeviceType::DEVICE_NAIVE;
}

Status CpuCropResizeNode::InferTopShapes() {
    shared_resource->net_input_w = dst_width;
    shared_resource->net_input_h = dst_height;

    top_blob_shapes     = {{max_batch, dst_height, dst_width, 3}};  // NHWC
    top_blob_data_types = {DataType::DATA_TYPE_UINT8};
    return COSMO_NN_OK;
}

size_t CpuCropResizeNode::GetTopCount() {
    return 1;
}

size_t CpuCropResizeNode::GetBottomCount() {
    return 2;
}

Status CpuCropResizeNode::PrepareRect(std::shared_ptr<Blob> host_rect, int image_w, int image_h) {
    calculated_rects.clear();

    auto desc   = host_rect->GetBlobDesc();
    auto handle = host_rect->GetHandle();

    if (desc.data_type != DataType::DATA_TYPE_INT32)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "rect data type must be int32.");

    auto dims = desc.dims;
    if (dims.at(1) != 4)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "rect shape must be [n, 4].");

    size_t n                = dims.at(0);
    int32_t* host_rect_data = reinterpret_cast<int32_t*>(handle.base);

    for (size_t i = 0; i < n; ++i) {
        int x1 = host_rect_data[0];
        int y1 = host_rect_data[1];
        int w  = host_rect_data[2];
        int h  = host_rect_data[3];
        host_rect_data += 4;

        int x2 = x1 + w - 1;
        int y2 = y1 + h - 1;

        if (square) {
            int base_len = 0;
            if (square_mode == 0)
                base_len = std::max(w, h);
            else if (square_mode == 1)
                base_len = std::min(w, h);
            else if (square_mode == 2)
                base_len = (w + h) / 2;
            else
                return Status(COSMO_NN_ERR_PARAM, "Invalid square_mode");

            int center_x = x1 + w / 2;
            int center_y = y1 + h / 2;

            x1 = center_x - static_cast<int>(base_len * (w_left_crop.at(0) + 0.5f));
            x2 = center_x + static_cast<int>(base_len * (w_right_crop.at(0) + 0.5f));
            y1 = center_y - static_cast<int>(base_len * (h_top_crop.at(0) + 0.5f));
            y2 = center_y + static_cast<int>(base_len * (h_bottom_crop.at(0) + 0.5f));
        } else {
            float top_ratio    = h_top_crop.at(0);
            float bottom_ratio = h_bottom_crop.at(0);
            float left_ratio   = w_left_crop.at(0);
            float right_ratio  = w_right_crop.at(0);

            x1 -= static_cast<int>(w * left_ratio);
            y1 -= static_cast<int>(h * top_ratio);
            x2 += static_cast<int>(w * right_ratio);
            y2 += static_cast<int>(h * bottom_ratio);
        }

        x1 = std::max(0, x1);
        y1 = std::max(0, y1);
        x2 = std::min(x2, image_w - 1);
        y2 = std::min(y2, image_h - 1);

        calculated_rects.push_back(x1);
        calculated_rects.push_back(y1);
        calculated_rects.push_back(x2 - x1 + 1);
        calculated_rects.push_back(y2 - y1 + 1);
    }
    return COSMO_NN_OK;
}

void CpuCropResizeNode::BilinearResize(const uint8_t* src, int src_w, int src_h, int channels, uint8_t* dst,
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

Status CpuCropResizeNode::Forward(std::vector<std::shared_ptr<Blob>>& image_blobs,
                                  std::vector<std::shared_ptr<Blob>>& rect_blobs,
                                  std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();

    auto top_blob = top_blobs.at(0);
    auto top_desc = top_blob->GetBlobDesc();
    auto top_dim  = top_desc.dims;

    int channels = 3;

    // Count total rects across all images
    int current_batch = 0;
    for (size_t i = 0; i < rect_blobs.size(); ++i) {
        current_batch += rect_blobs.at(i)->GetBlobDesc().dims.at(0);
    }
    if (current_batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size too large");

    // Update top shape
    top_dim.at(0) = current_batch;
    top_desc.dims = top_dim;
    top_blob->SetBlobDesc(top_desc);

    auto* top_data  = static_cast<uint8_t*>(top_blob->GetHandle().base);
    int single_size = dst_height * dst_width * channels;
    int processed   = 0;

    for (size_t i = 0; i < image_blobs.size(); ++i) {
        auto img_blob  = image_blobs.at(i);
        auto img_dims  = img_blob->GetBlobDesc().dims;
        int src_h      = img_dims.at(1);
        int src_w      = img_dims.at(2);
        auto* src_data = static_cast<const uint8_t*>(img_blob->GetHandle().base);

        auto rect_blob = rect_blobs.at(i);
        RETURN_ON_FAIL(PrepareRect(rect_blob, src_w, src_h));

        int n_rects = rect_blob->GetBlobDesc().dims.at(0);
        for (int j = 0; j < n_rects; ++j) {
            int rx = calculated_rects.at(4 * j);
            int ry = calculated_rects.at(4 * j + 1);
            int rw = calculated_rects.at(4 * j + 2);
            int rh = calculated_rects.at(4 * j + 3);

            // Extract crop from source
            std::vector<uint8_t> crop(rw * rh * channels);
            for (int y = 0; y < rh; y++) {
                std::memcpy(crop.data() + y * rw * channels, src_data + ((ry + y) * src_w + rx) * channels,
                            rw * channels);
            }

            // Resize crop to destination
            uint8_t* dst_ptr = top_data + processed * single_size;

            if (gravity == 0) {
                BilinearResize(crop.data(), rw, rh, channels, dst_ptr, dst_width, dst_height);
            } else {
                // Letterbox: keep aspect ratio
                float scale =
                    std::min(static_cast<float>(dst_width) / rw, static_cast<float>(dst_height) / rh);
                int new_w = static_cast<int>(rw * scale);
                int new_h = static_cast<int>(rh * scale);

                uint8_t pad_val = color.empty() ? 114 : static_cast<uint8_t>(color.at(0));
                std::memset(dst_ptr, pad_val, single_size);

                std::vector<uint8_t> tmp(new_w * new_h * channels);
                BilinearResize(crop.data(), rw, rh, channels, tmp.data(), new_w, new_h);

                int pad_x = (gravity == 1) ? (dst_width - new_w) / 2 : 0;
                int pad_y = (gravity == 1) ? (dst_height - new_h) / 2 : 0;

                for (int y = 0; y < new_h; y++) {
                    std::memcpy(dst_ptr + ((pad_y + y) * dst_width + pad_x) * channels,
                                tmp.data() + y * new_w * channels, new_w * channels);
                }
            }
            processed++;
        }
    }

    timer.Stop();
    return COSMO_NN_OK;
}

Status CpuCropResizeNode::Forward(std::vector<std::shared_ptr<Blob>>& /*bottom_blobs*/,
                                  std::vector<std::shared_ptr<Blob>>& /*top_blobs*/) {
    // Two-input version not used via this overload
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_CPU_BACKEND
