#include "nn/device/sophon/sophon_crop_resize_node.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "bmcv_api.h"
#include "bmcv_api_ext.h"
#include "bmlib_runtime.h"
#include "nn/device/sophon/sophon_image_guard.h"
#include "nn/device/sophon/sophon_memory_utils.h"
#include "nn/device/sophon/sophon_node.h"
#include "nn/node/node_type_utils.h"
#include "nn/utils/image_format_utils.h"
#include "nn/utils/rect.h"

namespace cosmo::nn {

namespace {

    constexpr int kSophonVppMaxDimension = 8192;

    bool GetBgrFrameSize(int width, int height, size_t* frame_size) {
        if (frame_size == nullptr || width <= 0 || height <= 0 || width > kSophonVppMaxDimension ||
            height > kSophonVppMaxDimension) {
            return false;
        }
        const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
        if (pixel_count > std::numeric_limits<size_t>::max() / 3) {
            return false;
        }
        *frame_size = pixel_count * 3;
        return true;
    }

    bool SafeScaledCoordinate(long double value, int64_t* result) {
        if (result == nullptr || !std::isfinite(value) ||
            value < static_cast<long double>(std::numeric_limits<int64_t>::min()) ||
            value > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
            return false;
        }
        *result = static_cast<int64_t>(value);
        return true;
    }

    Status RunCropResize(bm_handle_t handle, const bm_device_mem_t& source, int src_width, int src_height,
                         const bmcv_rect_t& requested_crop, const std::array<bm_device_mem_t, 3>& destination,
                         int dst_width, int dst_height, int gravity, const std::vector<int>& color) {
        SophonImageGuard source_image;
        auto ret = bm_image_create(handle, src_height, src_width, FORMAT_BGR_PACKED, DATA_TYPE_EXT_1N_BYTE,
                                   source_image.Out());
        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_CREAT_FAILED, "bm_image source create failed");
        }
        source_image.MarkCreated();
        auto source_view = source;
        ret              = bm_image_attach(source_image.Get(), &source_view);
        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_ATTACH_FAILED, "bm_image source attach failed");
        }

        SophonImageGuard destination_image;
        ret = bm_image_create(handle, dst_height, dst_width, FORMAT_BGR_PLANAR, DATA_TYPE_EXT_1N_BYTE,
                              destination_image.Out());
        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_CREAT_FAILED, "bm_image destination create failed");
        }
        destination_image.MarkCreated();
        auto destination_views = destination;
        ret                    = bm_image_attach(destination_image.Get(), destination_views.data());
        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_ATTACH_FAILED, "bm_image destination attach failed");
        }

        bmcv_rect_t crop = requested_crop;
        if (gravity == 0) {
            ret = bmcv_image_vpp_convert(handle, 1, source_image.Get(), destination_image.Out(), &crop,
                                         BMCV_INTER_NEAREST);
        } else if (gravity == 1) {
            const float scale = std::min(static_cast<float>(dst_width) / static_cast<float>(crop.crop_w),
                                         static_cast<float>(dst_height) / static_cast<float>(crop.crop_h));
            const int resized_width =
                std::clamp(static_cast<int>(std::round(crop.crop_w * scale)), 1, dst_width);
            const int resized_height =
                std::clamp(static_cast<int>(std::round(crop.crop_h * scale)), 1, dst_height);
            bmcv_padding_atrr_t padding{};
            padding.dst_crop_stx = (dst_width - resized_width) / 2;
            padding.dst_crop_sty = (dst_height - resized_height) / 2;
            padding.dst_crop_w   = resized_width;
            padding.dst_crop_h   = resized_height;
            padding.padding_b    = color[0];
            padding.padding_g    = color[1];
            padding.padding_r    = color[2];
            padding.if_memset    = 1;
            ret = bmcv_image_vpp_convert_padding(handle, 1, source_image.Get(), destination_image.Out(),
                                                 &padding, &crop, BMCV_INTER_NEAREST);
        } else if (gravity == 2) {
            const double source_ratio = static_cast<double>(crop.crop_w) / static_cast<double>(crop.crop_h);
            const double target_ratio = static_cast<double>(dst_width) / static_cast<double>(dst_height);
            if (source_ratio > target_ratio) {
                const auto covered_width =
                    static_cast<unsigned int>(std::max(1.0, std::floor(crop.crop_h * target_ratio)));
                crop.start_x += (crop.crop_w - covered_width) / 2;
                crop.crop_w = covered_width;
            } else {
                const auto covered_height =
                    static_cast<unsigned int>(std::max(1.0, std::floor(crop.crop_w / target_ratio)));
                crop.start_y += (crop.crop_h - covered_height) / 2;
                crop.crop_h = covered_height;
            }
            ret = bmcv_image_vpp_convert(handle, 1, source_image.Get(), destination_image.Out(), &crop,
                                         BMCV_INTER_NEAREST);
        } else {
            return Status(COSMO_NN_ERR_PARAM, "gravity not supported");
        }

        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_INFER_ERR, "bmcv crop resize failed");
        }
        return Status{COSMO_NN_OK};
    }

}  // namespace

SophonCropResizeNode::SophonCropResizeNode() : Node() {
    node_type     = NodeType::NODE_CROP_RESIZE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_CROP_RESIZE).append("_0");
    one_blob_only = false;
}

SophonCropResizeNode::~SophonCropResizeNode() = default;

void SophonCropResizeNode::LoadParam(Op* op) {
    if (!op) {
        return;
    }
    auto* crop_resize = dynamic_cast<CropResize*>(op);
    if (crop_resize == nullptr || crop_resize->dsize.size() < 2) {
        return;
    }

    type          = crop_resize->type;
    h_top_crop    = crop_resize->h_top_crop;
    h_bottom_crop = crop_resize->h_bottom_crop;
    w_left_crop   = crop_resize->w_left_crop;
    w_right_crop  = crop_resize->w_right_crop;
    square        = crop_resize->square;
    square_mode   = crop_resize->square_mode;
    dst_height    = crop_resize->dsize[0];
    dst_width     = crop_resize->dsize[1];
    gravity       = crop_resize->gravity;
    color         = crop_resize->color;
}

Status SophonCropResizeNode::InferTopShapes() {
    if (shared_resource == nullptr || dst_width <= 0 || dst_height <= 0 ||
        dst_width > kSophonVppMaxDimension || dst_height > kSophonVppMaxDimension) {
        return Status(COSMO_NN_ERR_PARAM, "crop resize output dimensions are invalid");
    }
    shared_resource->net_input_w = dst_width;
    shared_resource->net_input_h = dst_height;

    const int aligned_w = ALIGN(dst_width, 64);
    top_blob_shapes     = {{max_batch, dst_height, aligned_w, 3}};
    top_blob_data_types = {DataType::DATA_TYPE_UINT8};
    return Status{COSMO_NN_OK};
}

DeviceType SophonCropResizeNode::GetTopBlobDeviceType() {
    return DeviceType::DEVICE_SOPHON_TPU;
}

size_t SophonCropResizeNode::GetTopCount() {
    return 1;
}

size_t SophonCropResizeNode::GetBottomCount() {
    return 2;
}

Status SophonCropResizeNode::PrepareRect(std::shared_ptr<Blob> host_rect, Size image_size) {
    calculated_rects.clear();
    if (!host_rect || host_rect->GetHandle().base == nullptr || image_size.width <= 0 ||
        image_size.height <= 0) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "rect input is invalid");
    }

    const auto& desc = host_rect->GetBlobDesc();
    if (desc.data_type != DataType::DATA_TYPE_INT32 || desc.dims.size() != 2 || desc.dims[0] <= 0 ||
        desc.dims[1] != 4 || desc.dims[0] > max_batch) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "rect shape must be [n, 4]");
    }
    if (h_top_crop.empty() || h_bottom_crop.empty() || w_left_crop.empty() || w_right_crop.empty() ||
        (square && square_mode != 0 && square_mode != 1 && square_mode != 2)) {
        return Status(COSMO_NN_ERR_PARAM, "crop configuration is invalid");
    }

    const auto* rect_data = static_cast<const int32_t*>(host_rect->GetHandle().base);
    for (int i = 0; i < desc.dims[0]; ++i) {
        const int64_t x = rect_data[i * 4];
        const int64_t y = rect_data[i * 4 + 1];
        const int64_t w = rect_data[i * 4 + 2];
        const int64_t h = rect_data[i * 4 + 3];
        if (w <= 0 || h <= 0) {
            return Status(COSMO_NN_ERR_INVALID_INPUT, "rect dimensions must be positive");
        }

        int64_t left   = x;
        int64_t top    = y;
        int64_t right  = 0;
        int64_t bottom = 0;
        if (square) {
            int64_t base_length = 0;
            if (square_mode == 0) {
                base_length = std::max(w, h);
            } else if (square_mode == 1) {
                base_length = std::min(w, h);
            } else {
                if (w > std::numeric_limits<int64_t>::max() - h) {
                    return Status(COSMO_NN_ERR_INVALID_INPUT, "rect dimensions overflow");
                }
                base_length = (w + h) / 2;
            }
            const int64_t center_x = x + w / 2;
            const int64_t center_y = y + h / 2;
            if (!SafeScaledCoordinate(center_x - base_length * (w_left_crop[0] + 0.5L), &left) ||
                !SafeScaledCoordinate(center_x + base_length * (w_right_crop[0] + 0.5L), &right) ||
                !SafeScaledCoordinate(center_y - base_length * (h_top_crop[0] + 0.5L), &top) ||
                !SafeScaledCoordinate(center_y + base_length * (h_bottom_crop[0] + 0.5L), &bottom)) {
                return Status(COSMO_NN_ERR_INVALID_INPUT, "rect expansion overflow");
            }
        } else {
            if (!SafeScaledCoordinate(x - w * static_cast<long double>(w_left_crop[0]), &left) ||
                !SafeScaledCoordinate(x + w + w * static_cast<long double>(w_right_crop[0]), &right) ||
                !SafeScaledCoordinate(y - h * static_cast<long double>(h_top_crop[0]), &top) ||
                !SafeScaledCoordinate(y + h + h * static_cast<long double>(h_bottom_crop[0]), &bottom)) {
                return Status(COSMO_NN_ERR_INVALID_INPUT, "rect expansion overflow");
            }
        }

        left   = std::clamp<int64_t>(left, 0, image_size.width);
        right  = std::clamp<int64_t>(right, 0, image_size.width);
        top    = std::clamp<int64_t>(top, 0, image_size.height);
        bottom = std::clamp<int64_t>(bottom, 0, image_size.height);
        if (left >= right || top >= bottom) {
            return Status(COSMO_NN_ERR_INVALID_INPUT, "rect is outside image bounds");
        }

        calculated_rects.push_back(static_cast<int32_t>(left));
        calculated_rects.push_back(static_cast<int32_t>(top));
        calculated_rects.push_back(static_cast<int32_t>(right - left));
        calculated_rects.push_back(static_cast<int32_t>(bottom - top));
    }
    return Status{COSMO_NN_OK};
}

Status SophonCropResizeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                                     std::vector<std::shared_ptr<Blob>>& params,
                                     std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();
    RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blobs, top_blobs, true));
    RETURN_ON_FAIL(CheckNodeInputOutput(params, top_blobs, false));

    auto top_blob                = top_blobs.at(0);
    auto top_desc                = top_blob->GetBlobDesc();
    const ImageFormat bottom_fmt = bottom_blobs.at(0)->GetBlobDesc().image_format;
    if (ImageFormatIsGray(bottom_fmt)) {
        top_desc.image_format = IMAGE_GRAY;
    } else if (ImageFormatIsRGB(bottom_fmt)) {
        top_desc.image_format = IMAGE_RGB;
    } else if (ImageFormatIsBGR(bottom_fmt)) {
        top_desc.image_format = IMAGE_BGR;
    } else {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "image format not supported");
    }
    top_blob->SetBlobDesc(top_desc);
    RETURN_ON_FAIL(Forward(bottom_blobs, params, top_blob));
    timer.Stop();
    return Status{COSMO_NN_OK};
}

Status SophonCropResizeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                                     std::vector<std::shared_ptr<Blob>>& top_blobs) {
    return Status(COSMO_NN_ERR_INVALID_INPUT, "crop resize requires image and rect inputs");
}

Status SophonCropResizeNode::Forward(std::vector<std::shared_ptr<Blob>>& image_blobs,
                                     std::vector<std::shared_ptr<Blob>>& rect_blobs,
                                     std::shared_ptr<Blob> top_blob) {
    if (shared_resource == nullptr || shared_resource->m_handle == nullptr) {
        return Status(COSMO_NN_ERR_SOPHON_HANDLE_FAILED, "bm_handle_t is null");
    }
    if (image_blobs.empty() || image_blobs.size() != rect_blobs.size() || color.size() < 3) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "crop resize inputs are invalid");
    }

    size_t total_rect_count = 0;
    for (const auto& rect_blob : rect_blobs) {
        const auto& dims = rect_blob->GetBlobDesc().dims;
        if (dims.size() != 2 || dims[0] <= 0 || dims[0] > max_batch || dims[1] != 4 ||
            total_rect_count > static_cast<size_t>(max_batch - dims[0])) {
            return Status(COSMO_NN_ERR_INVALID_INPUT, "crop resize rect count is invalid");
        }
        total_rect_count += static_cast<size_t>(dims[0]);
    }
    if (total_rect_count == 0 || total_rect_count > static_cast<size_t>(max_batch)) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "crop resize batch size is invalid");
    }

    auto top_desc = top_blob->GetBlobDesc();
    if (top_desc.dims.size() != 4 || top_desc.dims[1] <= 0 || top_desc.dims[2] <= 0) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "crop resize output shape is invalid");
    }
    const int output_height  = top_desc.dims[1];
    const int output_width   = top_desc.dims[2];
    size_t output_frame_size = 0;
    if (!GetBgrFrameSize(output_width, output_height, &output_frame_size)) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "crop resize output size is invalid");
    }
    top_desc.dims[0] = static_cast<int>(total_rect_count);
    top_blob->SetBlobDesc(top_desc);

    const auto* output_base        = static_cast<const bm_device_mem_t*>(top_blob->GetHandle().base);
    const size_t output_plane_size = output_frame_size / 3;
    size_t output_index            = 0;
    for (size_t image_index = 0; image_index < image_blobs.size(); ++image_index) {
        const auto& image_desc = image_blobs[image_index]->GetBlobDesc();
        if (image_desc.dims.size() != 4 || image_desc.dims[0] != 1 || image_desc.dims[1] <= 0 ||
            image_desc.dims[2] <= 0) {
            return Status(COSMO_NN_ERR_INVALID_INPUT, "crop resize image shape is invalid");
        }
        const int source_height  = image_desc.dims[1];
        const int source_width   = image_desc.dims[2];
        size_t source_frame_size = 0;
        if (!GetBgrFrameSize(source_width, source_height, &source_frame_size)) {
            return Status(COSMO_NN_ERR_INVALID_INPUT, "crop resize source size is invalid");
        }
        RETURN_ON_FAIL(PrepareRect(rect_blobs[image_index], Size(source_width, source_height)));

        const auto* source_base =
            static_cast<const bm_device_mem_t*>(image_blobs[image_index]->GetHandle().base);
        bm_device_mem_t source{};
        if (!MakeDeviceMemoryView(*source_base, 0, source_frame_size, &source)) {
            return Status(COSMO_NN_ERR_INVALID_INPUT, "crop resize source buffer is too small");
        }

        const size_t rect_count = static_cast<size_t>(rect_blobs[image_index]->GetBlobDesc().dims[0]);
        for (size_t rect_index = 0; rect_index < rect_count; ++rect_index, ++output_index) {
            std::array<bm_device_mem_t, 3> destination{};
            if (!MakeThreePlaneDeviceMemoryView(*output_base, output_index * output_frame_size,
                                                output_plane_size, &destination)) {
                return Status(COSMO_NN_ERR_INVALID_INPUT, "crop resize output buffer is too small");
            }
            bmcv_rect_t crop{};
            crop.start_x = static_cast<unsigned int>(calculated_rects[rect_index * 4]);
            crop.start_y = static_cast<unsigned int>(calculated_rects[rect_index * 4 + 1]);
            crop.crop_w  = static_cast<unsigned int>(calculated_rects[rect_index * 4 + 2]);
            crop.crop_h  = static_cast<unsigned int>(calculated_rects[rect_index * 4 + 3]);
            RETURN_ON_FAIL(RunCropResize(shared_resource->m_handle, source, source_width, source_height, crop,
                                         destination, output_width, output_height, gravity, color));
        }
    }
    return Status{COSMO_NN_OK};
}

}  // namespace cosmo::nn
